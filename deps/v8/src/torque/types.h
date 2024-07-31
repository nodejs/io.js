// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_TYPES_H_
#define V8_TORQUE_TYPES_H_

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "src/base/optional.h"
#include "src/torque/ast.h"
#include "src/torque/constants.h"
#include "src/torque/source-positions.h"
#include "src/torque/utils.h"

namespace v8 {
namespace internal {
namespace torque {

class AggregateType;
struct Identifier;
class Macro;
class Method;
class GenericType;
class StructType;
class Type;
class ClassType;
class Value;
class Namespace;

class TypeBase {
 public:
  enum class Kind {
    kTopType,
    kAbstractType,
    kBuiltinPointerType,
    kUnionType,
    kBitFieldStructType,
    kStructType,
    kClassType
  };
  virtual ~TypeBase() = default;
  bool IsTopType() const { return kind() == Kind::kTopType; }
  bool IsAbstractType() const { return kind() == Kind::kAbstractType; }
  bool IsBuiltinPointerType() const {
    return kind() == Kind::kBuiltinPointerType;
  }
  bool IsUnionType() const { return kind() == Kind::kUnionType; }
  bool IsBitFieldStructType() const {
    return kind() == Kind::kBitFieldStructType;
  }
  bool IsStructType() const { return kind() == Kind::kStructType; }
  bool IsClassType() const { return kind() == Kind::kClassType; }
  bool IsAggregateType() const { return IsStructType() || IsClassType(); }

 protected:
  explicit TypeBase(Kind kind) : kind_(kind) {}
  Kind kind() const { return kind_; }

 private:
  const Kind kind_;
};

#define DECLARE_TYPE_BOILERPLATE(x)                         \
  static x* cast(TypeBase* declarable) {                    \
    DCHECK(declarable->Is##x());                            \
    return static_cast<x*>(declarable);                     \
  }                                                         \
  static const x* cast(const TypeBase* declarable) {        \
    DCHECK(declarable->Is##x());                            \
    return static_cast<const x*>(declarable);               \
  }                                                         \
  static x* DynamicCast(TypeBase* declarable) {             \
    if (!declarable) return nullptr;                        \
    if (!declarable->Is##x()) return nullptr;               \
    return static_cast<x*>(declarable);                     \
  }                                                         \
  static const x* DynamicCast(const TypeBase* declarable) { \
    if (!declarable) return nullptr;                        \
    if (!declarable->Is##x()) return nullptr;               \
    return static_cast<const x*>(declarable);               \
  }

using TypeVector = std::vector<const Type*>;

template <typename T>
struct SpecializationKey {
  T* generic;
  TypeVector specialized_types;
};

using MaybeSpecializationKey = base::Optional<SpecializationKey<GenericType>>;

struct TypeChecker {
  // The type of the object. This string is not guaranteed to correspond to a
  // C++ class, but just to a type checker function: for any type "Foo" here,
  // the function IsFoo must exist.
  std::string type;
  // If {type} is "MaybeObject", then {weak_ref_to} indicates the corresponding
  // strong object type. Otherwise, {weak_ref_to} is empty.
  std::string weak_ref_to;
};

class V8_EXPORT_PRIVATE Type : public TypeBase {
 public:
  Type& operator=(const Type& other) = delete;
  virtual bool IsSubtypeOf(const Type* supertype) const;

  // Default rendering for error messages etc.
  std::string ToString() const;

  // This name is not unique, but short and somewhat descriptive.
  // Used for naming generated code.
  virtual std::string SimpleName() const;

  enum class HandleKind { kIndirect, kDirect };
  std::string GetHandleTypeName(HandleKind kind,
                                const std::string& type_name) const;

  std::string TagglifiedCppTypeName() const;
  std::string HandlifiedCppTypeName(HandleKind kind) const;

  const Type* parent() const { return parent_; }
  bool IsVoid() const { return IsAbstractName(VOID_TYPE_STRING); }
  bool IsNever() const { return IsAbstractName(NEVER_TYPE_STRING); }
  bool IsBool() const { return IsAbstractName(BOOL_TYPE_STRING); }
  bool IsConstexprBool() const {
    return IsAbstractName(CONSTEXPR_BOOL_TYPE_STRING);
  }
  bool IsVoidOrNever() const { return IsVoid() || IsNever(); }
  bool IsFloat32() const { return IsAbstractName(FLOAT32_TYPE_STRING); }
  bool IsFloat64() const { return IsAbstractName(FLOAT64_TYPE_STRING); }
  std::string GetGeneratedTypeName() const;
  std::string GetGeneratedTNodeTypeName() const;
  virtual bool IsConstexpr() const {
    if (parent()) DCHECK(!parent()->IsConstexpr());
    return false;
  }
  virtual bool IsTransient() const { return false; }
  virtual const Type* NonConstexprVersion() const { return this; }
  std::string GetConstexprGeneratedTypeName() const;
  base::Optional<const ClassType*> ClassSupertype() const;
  base::Optional<const StructType*> StructSupertype() const;
  base::Optional<const AggregateType*> AggregateSupertype() const;
  virtual std::vector<TypeChecker> GetTypeCheckers() const { return {}; }
  virtual std::string GetRuntimeType() const;
  virtual std::string GetDebugType() const;
  static const Type* CommonSupertype(const Type* a, const Type* b);
  void AddAlias(std::string alias) const { aliases_.insert(std::move(alias)); }
  size_t id() const { return id_; }
  const MaybeSpecializationKey& GetSpecializedFrom() const {
    return specialized_from_;
  }

  static base::Optional<const Type*> MatchUnaryGeneric(const Type* type,
                                                       GenericType* generic);

  static std::string ComputeName(const std::string& basename,
                                 MaybeSpecializationKey specialized_from);
  virtual void SetConstexprVersion(const Type* type) const {
    constexpr_version_ = type;
  }

  virtual const Type* ConstexprVersion() const {
    if (constexpr_version_) return constexpr_version_;
    if (IsConstexpr()) return this;
    if (parent()) return parent()->ConstexprVersion();
    return nullptr;
  }

  virtual size_t AlignmentLog2() const;

 protected:
  Type(TypeBase::Kind kind, const Type* parent,
       MaybeSpecializationKey specialized_from = base::nullopt);
  Type(const Type& other) V8_NOEXCEPT;
  void set_parent(const Type* t) { parent_ = t; }
  int Depth() const;
  virtual std::string ToExplicitString() const = 0;
  virtual std::string GetGeneratedTypeNameImpl() const = 0;
  virtual std::string GetGeneratedTNodeTypeNameImpl() const = 0;
  virtual std::string SimpleNameImpl() const = 0;

 private:
  bool IsAbstractName(const std::string& name) const;

  // If {parent_} is not nullptr, then this type is a subtype of {parent_}.
  const Type* parent_;
  mutable std::set<std::string> aliases_;
  size_t id_;
  MaybeSpecializationKey specialized_from_;
  mutable const Type* constexpr_version_ = nullptr;
};

inline size_t hash_value(const TypeVector& types) {
  size_t hash = 0;
  for (const Type* t : types) {
    hash = base::hash_combine(hash, t);
  }
  return hash;
}

struct NameAndType {
  std::string name;
  const Type* type;
};

std::ostream& operator<<(std::ostream& os, const NameAndType& name_and_type);

struct Field {
  // TODO(danno): This likely should be refactored, the handling of the types
  // using the universal grab-bag utility with std::tie, as well as the
  // reliance of string types is quite clunky.
  std::tuple<size_t, std::string> GetFieldSizeInformation() const;

  void ValidateAlignment(ResidueClass at_offset) const;

  SourcePosition pos;
  const AggregateType* aggregate;
  base::Optional<ClassFieldIndexInfo> index;
  NameAndType name_and_type;

  // The byte offset of this field from the beginning of the containing class or
  // struct. Most structs are never packed together in memory, and are only used
  // to hold a batch of related CSA TNode values, in which case |offset| is
  // irrelevant.
  // The offset may be unknown because the field is after an indexed field or
  // because we don't support the struct field for on-heap layouts.
  base::Optional<size_t> offset;

  bool custom_weak_marking;
  bool const_qualified;
  FieldSynchronization read_synchronization;
  FieldSynchronization write_synchronization;
};

std::ostream& operator<<(std::ostream& os, const Field& name_and_type);

class TopType final : public Type {
 public:
  DECLARE_TYPE_BOILERPLATE(TopType)
  std::string GetGeneratedTypeNameImpl() const override { UNREACHABLE(); }
  std::string GetGeneratedTNodeTypeNameImpl() const override {
    return source_type_->GetGeneratedTNodeTypeName();
  }
  std::string ToExplicitString() const override {
    std::stringstream s;
    s << "inaccessible " + source_type_->ToString();
    return s.str();
  }

  const Type* source_type() const { return source_type_; }
  const std::string reason() const { return reason_; }

 private:
  friend class TypeOracle;
  explicit TopType(std::string reason, const Type* source_type)
      : Type(Kind::kTopType, nullptr),
        reason_(std::move(reason)),
        source_type_(source_type) {}
  std::string SimpleNameImpl() const override { return "TopType"; }

  std::string reason_;
  const Type* source_type_;
};

class AbstractType final : public Type {
 public:
  DECLARE_TYPE_BOILERPLATE(AbstractType)
  const std::string& name() const { return name_; }
  std::string ToExplicitString() const override { return name(); }
  std::string GetGeneratedTypeNameImpl() const override;
  std::string GetGeneratedTNodeTypeNameImpl() const override;
  bool IsConstexpr() const final {
    const bool is_constexpr = flags_ & AbstractTypeFlag::kConstexpr;
    DCHECK_IMPLIES(non_constexpr_version_ != nullptr, is_constexpr);
    return is_constexpr;
  }

  const Type* NonConstexprVersion() const override {
    if (non_constexpr_version_) return non_constexpr_version_;
    if (!IsConstexpr()) return this;
    if (parent()) return parent()->NonConstexprVersion();
    return nullptr;
  }

  std::vector<TypeChecker> GetTypeCheckers() const override;

  size_t AlignmentLog2() const override;

 private:
  friend class TypeOracle;
  AbstractType(const Type* parent, AbstractTypeFlags flags,
               const std::string& name, const std::string& generated_type,
               const Type* non_constexpr_version,
               MaybeSpecializationKey specialized_from)
      : Type(Kind::kAbstractType, parent, specialized_from),
        flags_(flags),
        name_(name),
        generated_type_(generated_type),
        non_constexpr_version_(non_constexpr_version) {
    if (parent) DCHECK_EQ(parent->IsConstexpr(), IsConstexpr());
    DCHECK_EQ(IsConstexprName(name), IsConstexpr());
    DCHECK_IMPLIES(non_constexpr_version_ != nullptr, IsConstexpr());
    DCHECK(!(IsConstexpr() && (flags_ & AbstractTypeFlag::kTransient)));
  }

  std::string SimpleNameImpl() const override {
    if (IsConstexpr()) {
      const Type* non_constexpr_version = NonConstexprVersion();
      if (non_constexpr_version == nullptr) {
        ReportError("Cannot find non-constexpr type corresponding to ", *this);
      }
      return "constexpr_" + non_constexpr_version->SimpleName();
    }
    return name();
  }

  bool IsTransient() const override {
    return flags_ & AbstractTypeFlag::kTransient;
  }

  bool UseParentTypeChecker() const {
    return flags_ & AbstractTypeFlag::kUseParentTypeChecker;
  }

  AbstractTypeFlags flags_;
  const std::string name_;
  const std::string generated_type_;
  const Type* non_constexpr_version_;
};

// For now, builtin pointers are restricted to Torque-defined builtins.
class V8_EXPORT_PRIVATE BuiltinPointerType final : public Type {
 public:
  DECLARE_TYPE_BOILERPLATE(BuiltinPointerType)
  std::string ToExplicitString() const override;
  std::string GetGeneratedTypeNameImpl() const override {
    return parent()->GetGeneratedTypeName();
  }
  std::string GetGeneratedTNodeTypeNameImpl() const override {
    return parent()->GetGeneratedTNodeTypeName();
  }

  const TypeVector& parameter_types() const { return parameter_types_; }
  const Type* return_type() const { return return_type_; }

  friend size_t hash_value(const BuiltinPointerType& p) {
    size_t result = base::hash_value(p.return_type_);
    for (const Type* parameter : p.parameter_types_) {
      result = base::hash_combine(result, parameter);
    }
    return result;
  }
  bool operator==(const BuiltinPointerType& other) const {
    return parameter_types_ == other.parameter_types_ &&
           return_type_ == other.return_type_;
  }
  size_t function_pointer_type_id() const { return function_pointer_type_id_; }

  std::vector<TypeChecker> GetTypeCheckers() const override {
    return {{"Smi", ""}};
  }

  bool HasContextParameter() const;

 private:
  friend class TypeOracle;
  BuiltinPointerType(const Type* parent, TypeVector parameter_types,
                     const Type* return_type, size_t function_pointer_type_id)
      : Type(Kind::kBuiltinPointerType, parent),
        parameter_types_(parameter_types),
        return_type_(return_type),
        function_pointer_type_id_(function_pointer_type_id) {}
  std::string SimpleNameImpl() const override;

  const TypeVector parameter_types_;
  const Type* const return_type_;
  const size_t function_pointer_type_id_;
};

bool operator<(const Type& a, const Type& b);
struct TypeLess {
  bool operator()(const Type* const a, const Type* const b) const {
    return *a < *b;
  }
};

class V8_EXPORT_PRIVATE UnionType final : public Type {
 public:
  DECLARE_TYPE_BOILERPLATE(UnionType)
  std::string GetGeneratedTypeNameImpl() const override {
    return "TNode<" + GetGeneratedTNodeTypeName() + ">";
  }
  std::string GetGeneratedTNodeTypeNameImpl() const override;
  std::string GetRuntimeType() const override {
    return parent()->GetRuntimeType();
  }
  std::string GetDebugType() const override { return parent()->GetDebugType(); }

  friend size_t hash_value(const UnionType& p) {
    size_t result = 0;
    for (const Type* t : p.types_) {
      result = base::hash_combine(result, t);
    }
    return result;
  }
  bool operator==(const UnionType& other) const {
    return types_ == other.types_;
  }

  base::Optional<const Type*> GetSingleMember() const {
    if (types_.size() == 1) {
      DCHECK_EQ(*types_.begin(), parent());
      return *types_.begin();
    }
    return base::nullopt;
  }

  bool IsSubtypeOf(const Type* other) const override {
    for (const Type* member : types_) {
      if (!member->IsSubtypeOf(other)) return false;
    }
    return true;
  }

  bool IsSupertypeOf(const Type* other) const {
    for (const Type* member : types_) {
      if (other->IsSubtypeOf(member)) {
        return true;
      }
    }
    return false;
  }

  bool IsTransient() const override {
    for (const Type* member : types_) {
      if (member->IsTransient()) {
        return true;
      }
    }
    return false;
  }

  bool IsConstexpr() const override { return parent()->IsConstexpr(); }

  const Type* NonConstexprVersion() const override {
    if (!IsConstexpr()) return this;
    return parent()->NonConstexprVersion();
  }

  void Extend(const Type* t) {
    if (const UnionType* union_type = UnionType::DynamicCast(t)) {
      for (const Type* member : union_type->types_) {
        Extend(member);
      }
    } else {
      if (t->IsSubtypeOf(this)) return;
      set_parent(CommonSupertype(parent(), t));
      EraseIf(&types_,
              [&](const Type* member) { return member->IsSubtypeOf(t); });
      types_.insert(t);
    }
  }
  std::string ToExplicitString() const override;

  void Subtract(const Type* t);

  static UnionType FromType(const Type* t) {
    const UnionType* union_type = UnionType::DynamicCast(t);
    return union_type ? UnionType(*union_type) : UnionType(t);
  }

  std::vector<TypeChecker> GetTypeCheckers() const override {
    std::vector<TypeChecker> result;
    for (const Type* member : types_) {
      std::vector<TypeChecker> sub_result = member->GetTypeCheckers();
      result.insert(result.end(), sub_result.begin(), sub_result.end());
    }
    return result;
  }

 private:
  explicit UnionType(const Type* t) : Type(Kind::kUnionType, t), types_({t}) {}
  void RecomputeParent();
  std::string SimpleNameImpl() const override;

  std::set<const Type*, TypeLess> types_;
};

const Type* SubtractType(const Type* a, const Type* b);

struct BitField {
  SourcePosition pos;
  NameAndType name_and_type;
  int offset;
  int num_bits;
};

class V8_EXPORT_PRIVATE BitFieldStructType final : public Type {
 public:
  DECLARE_TYPE_BOILERPLATE(BitFieldStructType)
  std::string ToExplicitString() const override;
  std::string GetGeneratedTypeNameImpl() const override {
    return parent()->GetGeneratedTypeName();
  }
  std::string GetGeneratedTNodeTypeNameImpl() const override {
    return parent()->GetGeneratedTNodeTypeName();
  }

  std::vector<TypeChecker> GetTypeCheckers() const override {
    return parent()->GetTypeCheckers();
  }

  void SetConstexprVersion(const Type*) const override { UNREACHABLE(); }
  const Type* ConstexprVersion() const override {
    return parent()->ConstexprVersion();
  }

  void RegisterField(BitField field) { fields_.push_back(std::move(field)); }

  const std::string& name() const { return decl_->name->value; }
  const std::vector<BitField>& fields() const { return fields_; }

  const BitField& LookupField(const std::string& name) const;

  const SourcePosition GetPosition() const { return decl_->pos; }

 private:
  friend class TypeOracle;
  BitFieldStructType(Namespace* nspace, const Type* parent,
                     const BitFieldStructDeclaration* decl)
      : Type(Kind::kBitFieldStructType, parent),
        namespace_(nspace),
        decl_(decl) {}
  std::string SimpleNameImpl() const override { return name(); }

  Namespace* namespace_;
  const BitFieldStructDeclaration* decl_;
  std::vector<BitField> fields_;
};

class AggregateType : public Type {
 public:
  DECLARE_TYPE_BOILERPLATE(AggregateType)
  std::string GetGeneratedTypeNameImpl() const override { UNREACHABLE(); }
  std::string GetGeneratedTNodeTypeNameImpl() const override { UNREACHABLE(); }

  virtual void Finalize() const = 0;

  void SetFields(std::vector<Field> fields) { fields_ = std::move(fields); }
  const std::vector<Field>& fields() const {
    if (!is_finalized_) Finalize();
    return fields_;
  }
  bool HasField(const std::string& name) const;
  const Field& LookupField(const std::string& name) const;
  const std::string& name() const { return name_; }
  Namespace* nspace() const { return namespace_; }

  virtual const Field& RegisterField(Field field) {
    fields_.push_back(field);
    return fields_.back();
  }

  void RegisterMethod(Method* method) { methods_.push_back(method); }
  const std::vector<Method*>& Methods() const {
    if (!is_finalized_) Finalize();
    return methods_;
  }
  std::vector<Method*> Methods(const std::string& name) const;

  std::vector<const AggregateType*> GetHierarchy() const;
  std::vector<TypeChecker> GetTypeCheckers() const override {
    return {{name_, ""}};
  }

  const Field& LastField() const {
    for (base::Optional<const AggregateType*> current = this;
         current.has_value();
         current = (*current)->parent()->AggregateSupertype()) {
      const std::vector<Field>& fields = (*current)->fields_;
      if (!fields.empty()) return fields[fields.size() - 1];
    }
    ReportError("Can't get last field of empty aggregate type");
  }

 protected:
  AggregateType(Kind kind, const Type* parent, Namespace* nspace,
                const std::string& name,
                MaybeSpecializationKey specialized_from = base::nullopt)
      : Type(kind, parent, specialized_from),
        is_finalized_(false),
        namespace_(nspace),
        name_(name) {}

  void CheckForDuplicateFields() const;
  // Use this lookup if you do not want to trigger finalization on this type.
  const Field& LookupFieldInternal(const std::string& name) const;
  std::string SimpleNameImpl() const override { return name_; }

 protected:
  mutable bool is_finalized_;
  std::vector<Field> fields_;

 private:
  Namespace* namespace_;
  std::string name_;
  std::vector<Method*> methods_;
};

class StructType final : public AggregateType {
 public:
  DECLARE_TYPE_BOILERPLATE(StructType)

  std::string GetGeneratedTypeNameImpl() const override;

  // Returns the sum of the size of all members.
  size_t PackedSize() const;

  size_t AlignmentLog2() const override;

  enum class ClassificationFlag {
    kEmpty = 0,
    kStrongTagged = 1 << 0,
    kWeakTagged = 1 << 1,
    kUntagged = 1 << 2,
  };
  using Classification = base::Flags<ClassificationFlag>;

  // Classifies a struct as containing tagged data, untagged data, or both.
  Classification ClassifyContents() const;

  SourcePosition GetPosition() const { return decl_->pos; }

 private:
  friend class TypeOracle;
  StructType(Namespace* nspace, const StructDeclaration* decl,
             MaybeSpecializationKey specialized_from = base::nullopt);

  void Finalize() const override;
  std::string ToExplicitString() const override;
  std::string SimpleNameImpl() const override;

  const StructDeclaration* decl_;
  std::string generated_type_name_;
};

class TypeAlias;

enum class ObjectSlotKind : uint8_t {
  kNoPointer,
  kStrongPointer,
  kMaybeObjectPointer,
  kCustomWeakPointer
};

inline base::Optional<ObjectSlotKind> Combine(ObjectSlotKind a,
                                              ObjectSlotKind b) {
  if (a == b) return {a};
  if (std::min(a, b) == ObjectSlotKind::kStrongPointer &&
      std::max(a, b) == ObjectSlotKind::kMaybeObjectPointer) {
    return {ObjectSlotKind::kMaybeObjectPointer};
  }
  return base::nullopt;
}

class ClassType final : public AggregateType {
 public:
  DECLARE_TYPE_BOILERPLATE(ClassType)
  std::string ToExplicitString() const override;
  std::string GetGeneratedTypeNameImpl() const override;
  std::string GetGeneratedTNodeTypeNameImpl() const override;
  bool IsExtern() const { return flags_ & ClassFlag::kExtern; }
  bool ShouldGeneratePrint() const {
    if (flags_ & ClassFlag::kCppObjectDefinition) return false;
    if (flags_ & ClassFlag::kCppObjectLayoutDefinition) return false;
    if (!IsExtern()) return true;
    if (!ShouldGenerateCppClassDefinitions()) return false;
    return !IsAbstract() && !HasUndefinedLayout();
  }
  bool ShouldGenerateVerify() const {
    if (flags_ & ClassFlag::kCppObjectDefinition) return false;
    if (flags_ & ClassFlag::kCppObjectLayoutDefinition) return false;
    if (!IsExtern()) return true;
    if (!ShouldGenerateCppClassDefinitions()) return false;
    return !HasUndefinedLayout() && !IsShape();
  }
  bool ShouldGenerateBodyDescriptor() const {
    if (flags_ & ClassFlag::kCppObjectDefinition) return false;
    if (flags_ & ClassFlag::kCppObjectLayoutDefinition) return false;
    if (flags_ & ClassFlag::kGenerateBodyDescriptor) return true;
    return !IsAbstract() && !IsExtern();
  }
  bool DoNotGenerateCast() const {
    return flags_ & ClassFlag::kDoNotGenerateCast;
  }
  bool IsTransient() const override { return flags_ & ClassFlag::kTransient; }
  bool IsAbstract() const { return flags_ & ClassFlag::kAbstract; }
  bool IsLayoutDefinedInCpp() const {
    return flags_ & ClassFlag::kCppObjectLayoutDefinition;
  }
  bool HasSameInstanceTypeAsParent() const {
    return flags_ & ClassFlag::kHasSameInstanceTypeAsParent;
  }
  bool ShouldGenerateCppClassDefinitions() const {
    if (flags_ & ClassFlag::kCppObjectDefinition) return false;
    if (flags_ & ClassFlag::kCppObjectLayoutDefinition) return false;
    return (flags_ & ClassFlag::kGenerateCppClassDefinitions) || !IsExtern();
  }
  bool ShouldGenerateCppObjectDefinitionAsserts() const {
    return flags_ & ClassFlag::kCppObjectDefinition;
  }
  bool ShouldGenerateCppObjectLayoutDefinitionAsserts() const {
    return flags_ & ClassFlag::kCppObjectLayoutDefinition &&
           flags_ & ClassFlag::kGenerateCppClassDefinitions;
  }
  bool ShouldGenerateFullClassDefinition() const { return !IsExtern(); }
  bool ShouldGenerateUniqueMap() const {
    return (flags_ & ClassFlag::kGenerateUniqueMap) ||
           (!IsExtern() && !IsAbstract());
  }
  bool ShouldGenerateFactoryFunction() const {
    return (flags_ & ClassFlag::kGenerateFactoryFunction) ||
           (ShouldExport() && !IsAbstract());
  }
  bool ShouldExport() const { return flags_ & ClassFlag::kExport; }
  bool IsShape() const { return flags_ & ClassFlag::kIsShape; }
  bool HasStaticSize() const;
  size_t header_size() const {
    if (!is_finalized_) Finalize();
    return header_size_;
  }
  ResidueClass size() const {
    if (!is_finalized_) Finalize();
    return size_;
  }
  const ClassType* GetSuperClass() const {
    if (parent() == nullptr) return nullptr;
    return parent()->IsClassType() ? ClassType::DynamicCast(parent()) : nullptr;
  }
  void GenerateAccessors();
  bool AllowInstantiation() const;
  const Field& RegisterField(Field field) override {
    return AggregateType::RegisterField(field);
  }
  void Finalize() const override;

  std::vector<Field> ComputeAllFields() const;
  std::vector<Field> ComputeHeaderFields() const;
  std::vector<Field> ComputeArrayFields() const;
  // The slots of an object are the tagged pointer sized offsets in an object
  // that may or may not require GC visiting. These helper functions determine
  // what kind of GC visiting the individual slots require.
  std::vector<ObjectSlotKind> ComputeHeaderSlotKinds() const;
  base::Optional<ObjectSlotKind> ComputeArraySlotKind() const;
  bool HasNoPointerSlotsExceptMap() const;
  bool HasIndexedFieldsIncludingInParents() const;
  const Field* GetFieldPreceding(size_t field_index) const;

  // Given that the field exists in this class or a superclass, returns the
  // specific class that declared the field.
  const ClassType* GetClassDeclaringField(const Field& f) const;

  std::string GetSliceMacroName(const Field& field) const;

  const InstanceTypeConstraints& GetInstanceTypeConstraints() const {
    return decl_->instance_type_constraints;
  }
  bool IsHighestInstanceTypeWithinParent() const {
    return flags_ & ClassFlag::kHighestInstanceTypeWithinParent;
  }
  bool IsLowestInstanceTypeWithinParent() const {
    return flags_ & ClassFlag::kLowestInstanceTypeWithinParent;
  }
  bool HasUndefinedLayout() const {
    return flags_ & ClassFlag::kUndefinedLayout;
  }
  SourcePosition GetPosition() const { return decl_->pos; }
  SourceId AttributedToFile() const;

  // TODO(turbofan): We should no longer pass around types as const pointers, so
  // that we can avoid mutable fields and const initializers for
  // late-initialized portions of types like this one.
  void InitializeInstanceTypes(base::Optional<int> own,
                               base::Optional<std::pair<int, int>> range) const;
  base::Optional<int> OwnInstanceType() const;
  base::Optional<std::pair<int, int>> InstanceTypeRange() const;

 private:
  friend class TypeOracle;
  friend class TypeVisitor;
  ClassType(const Type* parent, Namespace* nspace, const std::string& name,
            ClassFlags flags, const std::string& generates,
            const ClassDeclaration* decl, const TypeAlias* alias);

  void GenerateSliceAccessor(size_t field_index);

  size_t header_size_;
  ResidueClass size_;
  mutable ClassFlags flags_;
  const std::string generates_;
  const ClassDeclaration* decl_;
  const TypeAlias* alias_;
  mutable base::Optional<int> own_instance_type_;
  mutable base::Optional<std::pair<int, int>> instance_type_range_;
};

inline std::ostream& operator<<(std::ostream& os, const Type& t) {
  os << t.ToString();
  return os;
}

template <bool success = false>
std::ostream& operator<<(std::ostream& os, const Type* t) {
  static_assert(success,
                "Using Type* with an ostream is usually a mistake. Did you "
                "mean to use Type& instead? If you actually intended to print "
                "a pointer, use static_cast<const void*>.");
  return os;
}

// Don't emit an error if a Type* is printed due to CHECK macros.
inline std::ostream& operator<<(base::CheckMessageStream& os, const Type* t) {
  return os << static_cast<const void*>(t);
}

class VisitResult {
 public:
  VisitResult() = default;
  VisitResult(const Type* type, const std::string& constexpr_value)
      : type_(type), constexpr_value_(constexpr_value) {
    DCHECK(type->IsConstexpr());
  }
  static VisitResult NeverResult();
  static VisitResult TopTypeResult(std::string top_reason,
                                   const Type* from_type);
  VisitResult(const Type* type, StackRange stack_range)
      : type_(type), stack_range_(stack_range) {
    DCHECK(!type->IsConstexpr());
  }
  const Type* type() const { return type_; }
  const std::string& constexpr_value() const { return *constexpr_value_; }
  const StackRange& stack_range() const { return *stack_range_; }
  void SetType(const Type* new_type) { type_ = new_type; }
  bool IsOnStack() const { return stack_range_ != base::nullopt; }
  bool operator==(const VisitResult& other) const {
    return type_ == other.type_ && constexpr_value_ == other.constexpr_value_ &&
           stack_range_ == other.stack_range_;
  }

 private:
  const Type* type_ = nullptr;
  base::Optional<std::string> constexpr_value_;
  base::Optional<StackRange> stack_range_;
};

VisitResult ProjectStructField(VisitResult structure,
                               const std::string& fieldname);

class VisitResultVector : public std::vector<VisitResult> {
 public:
  VisitResultVector() : std::vector<VisitResult>() {}
  VisitResultVector(std::initializer_list<VisitResult> init)
      : std::vector<VisitResult>(init) {}
  TypeVector ComputeTypeVector() const {
    TypeVector result;
    for (auto& visit_result : *this) {
      result.push_back(visit_result.type());
    }
    return result;
  }
};

std::ostream& operator<<(std::ostream& os, const TypeVector& types);

using NameAndTypeVector = std::vector<NameAndType>;

struct LabelDefinition {
  std::string name;
  NameAndTypeVector parameters;
};

using LabelDefinitionVector = std::vector<LabelDefinition>;

struct LabelDeclaration {
  Identifier* name;
  TypeVector types;
};

using LabelDeclarationVector = std::vector<LabelDeclaration>;

struct ParameterTypes {
  TypeVector types;
  bool var_args;
};

std::ostream& operator<<(std::ostream& os, const ParameterTypes& parameters);

enum class ParameterMode { kProcessImplicit, kIgnoreImplicit };

using NameVector = std::vector<Identifier*>;

struct Signature {
  Signature(NameVector n, base::Optional<std::string> arguments_variable,
            ParameterTypes p, size_t i, const Type* r, LabelDeclarationVector l,
            bool transitioning)
      : parameter_names(std::move(n)),
        arguments_variable(arguments_variable),
        parameter_types(std::move(p)),
        implicit_count(i),
        return_type(r),
        labels(std::move(l)),
        transitioning(transitioning) {}
  Signature() = default;
  const TypeVector& types() const { return parameter_types.types; }
  NameVector parameter_names;
  base::Optional<std::string> arguments_variable;
  ParameterTypes parameter_types;
  size_t implicit_count = 0;
  size_t ExplicitCount() const { return types().size() - implicit_count; }
  const Type* return_type;
  LabelDeclarationVector labels;
  bool transitioning = false;
  bool HasSameTypesAs(
      const Signature& other,
      ParameterMode mode = ParameterMode::kProcessImplicit) const;
  TypeVector GetImplicitTypes() const {
    return TypeVector(parameter_types.types.begin(),
                      parameter_types.types.begin() + implicit_count);
  }
  TypeVector GetExplicitTypes() const {
    return TypeVector(parameter_types.types.begin() + implicit_count,
                      parameter_types.types.end());
  }
  bool HasContextParameter() const;
};

void PrintSignature(std::ostream& os, const Signature& sig, bool with_names);
std::ostream& operator<<(std::ostream& os, const Signature& sig);

bool IsAssignableFrom(const Type* to, const Type* from);

TypeVector LowerType(const Type* type);
size_t LoweredSlotCount(const Type* type);
TypeVector LowerParameterTypes(const TypeVector& parameters);
TypeVector LowerParameterTypes(const ParameterTypes& parameter_types,
                               size_t vararg_count = 0);

base::Optional<std::tuple<size_t, std::string>> SizeOf(const Type* type);
bool IsAnyUnsignedInteger(const Type* type);
bool IsAllowedAsBitField(const Type* type);
bool IsPointerSizeIntegralType(const Type* type);
bool Is32BitIntegralType(const Type* type);

base::Optional<NameAndType> ExtractSimpleFieldArraySize(
    const ClassType& class_type, Expression* array_size);

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_TYPES_H_
