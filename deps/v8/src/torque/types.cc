// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/types.h"

#include <cmath>
#include <iostream>
#include <optional>

#include "src/base/bits.h"
#include "src/torque/ast.h"
#include "src/torque/declarable.h"
#include "src/torque/global-context.h"
#include "src/torque/source-positions.h"
#include "src/torque/type-oracle.h"
#include "src/torque/type-visitor.h"

namespace v8::internal::torque {

// This custom copy constructor doesn't copy aliases_ and id_ because they
// should be distinct for each type.
Type::Type(const Type& other) V8_NOEXCEPT
    : TypeBase(other),
      parent_(other.parent_),
      aliases_(),
      id_(TypeOracle::FreshTypeId()),
      constexpr_version_(other.constexpr_version_) {}
Type::Type(TypeBase::Kind kind, const Type* parent,
           MaybeSpecializationKey specialized_from)
    : TypeBase(kind),
      parent_(parent),
      id_(TypeOracle::FreshTypeId()),
      specialized_from_(specialized_from),
      constexpr_version_(nullptr) {}

std::string Type::ToString() const {
  if (aliases_.empty())
    return ComputeName(ToExplicitString(), GetSpecializedFrom());
  if (aliases_.size() == 1) return *aliases_.begin();
  std::stringstream result;
  int i = 0;
  for (const std::string& alias : aliases_) {
    if (i == 0) {
      result << alias << " (aka. ";
    } else if (i == 1) {
      result << alias;
    } else {
      result << ", " << alias;
    }
    ++i;
  }
  result << ")";
  return result.str();
}

std::string Type::SimpleName() const {
  if (aliases_.empty()) {
    std::stringstream result;
    result << SimpleNameImpl();
    if (GetSpecializedFrom()) {
      for (const Type* t : GetSpecializedFrom()->specialized_types) {
        result << "_" << t->SimpleName();
      }
    }
    return result.str();
  }
  return *aliases_.begin();
}

std::string Type::GetHandleTypeName(HandleKind kind,
                                    const std::string& type_name) const {
  switch (kind) {
    case HandleKind::kIndirect:
      return "Handle<" + type_name + ">";
    case HandleKind::kDirect:
      return "DirectHandle<" + type_name + ">";
  }
}

// TODO(danno): HandlifiedCppTypeName should be used universally in Torque
// where the C++ type of a Torque object is required.
std::string Type::HandlifiedCppTypeName(HandleKind kind) const {
  if (IsSubtypeOf(TypeOracle::GetSmiType())) return "int";
  if (IsSubtypeOf(TypeOracle::GetTaggedType())) {
    return GetHandleTypeName(kind, GetConstexprGeneratedTypeName());
  } else {
    return GetConstexprGeneratedTypeName();
  }
}

std::string Type::TagglifiedCppTypeName() const {
  if (IsSubtypeOf(TypeOracle::GetSmiType())) return "int";
  if (IsSubtypeOf(TypeOracle::GetTaggedType())) {
    return "Tagged<" + GetConstexprGeneratedTypeName() + ">";
  } else {
    return GetConstexprGeneratedTypeName();
  }
}

bool Type::IsSubtypeOf(const Type* supertype) const {
  if (supertype->IsTopType()) return true;
  if (IsNever()) return true;
  if (const UnionType* union_type = UnionType::DynamicCast(supertype)) {
    return union_type->IsSupertypeOf(this);
  }
  const Type* subtype = this;
  while (subtype != nullptr) {
    if (subtype == supertype) return true;
    subtype = subtype->parent();
  }
  return false;
}

std::string Type::GetConstexprGeneratedTypeName() const {
  const Type* constexpr_version = ConstexprVersion();
  if (constexpr_version == nullptr) {
    Error("Type '", ToString(), "' requires a constexpr representation");
    return "";
  }
  return constexpr_version->GetGeneratedTypeName();
}

std::optional<const ClassType*> Type::ClassSupertype() const {
  for (const Type* t = this; t != nullptr; t = t->parent()) {
    if (auto* class_type = ClassType::DynamicCast(t)) {
      return class_type;
    }
  }
  return std::nullopt;
}

std::optional<const StructType*> Type::StructSupertype() const {
  for (const Type* t = this; t != nullptr; t = t->parent()) {
    if (auto* struct_type = StructType::DynamicCast(t)) {
      return struct_type;
    }
  }
  return std::nullopt;
}

std::optional<const AggregateType*> Type::AggregateSupertype() const {
  for (const Type* t = this; t != nullptr; t = t->parent()) {
    if (auto* aggregate_type = AggregateType::DynamicCast(t)) {
      return aggregate_type;
    }
  }
  return std::nullopt;
}

// static
const Type* Type::CommonSupertype(const Type* a, const Type* b) {
  int diff = a->Depth() - b->Depth();
  const Type* a_supertype = a;
  const Type* b_supertype = b;
  for (; diff > 0; --diff) a_supertype = a_supertype->parent();
  for (; diff < 0; ++diff) b_supertype = b_supertype->parent();
  while (a_supertype && b_supertype) {
    if (a_supertype == b_supertype) return a_supertype;
    a_supertype = a_supertype->parent();
    b_supertype = b_supertype->parent();
  }
  ReportError("types " + a->ToString() + " and " + b->ToString() +
              " have no common supertype");
}

int Type::Depth() const {
  int result = 0;
  for (const Type* current = parent_; current; current = current->parent_) {
    ++result;
  }
  return result;
}

bool Type::IsAbstractName(const std::string& name) const {
  if (!IsAbstractType()) return false;
  return AbstractType::cast(this)->name() == name;
}

std::string Type::GetGeneratedTypeName() const {
  std::string result = GetGeneratedTypeNameImpl();
  if (result.empty() || result == "TNode<>") {
    ReportError("Generated type is required for type '", ToString(),
                "'. Use 'generates' clause in definition.");
  }
  return result;
}

std::string Type::GetGeneratedTNodeTypeName() const {
  std::string result = GetGeneratedTNodeTypeNameImpl();
  if (result.empty() || IsConstexpr()) {
    ReportError("Generated TNode type is required for type '", ToString(),
                "'. Use 'generates' clause in definition.");
  }
  return result;
}

std::string AbstractType::GetGeneratedTypeNameImpl() const {
  // A special case that is not very well represented by the "generates"
  // syntax in the .tq files: Lazy<T> represents a std::function that
  // produces a TNode of the wrapped type.
  if (std::optional<const Type*> type_wrapped_in_lazy =
          Type::MatchUnaryGeneric(this, TypeOracle::GetLazyGeneric())) {
    DCHECK(!IsConstexpr());
    return "std::function<" + (*type_wrapped_in_lazy)->GetGeneratedTypeName() +
           "()>";
  }

  if (generated_type_.empty()) {
    return parent()->GetGeneratedTypeName();
  }
  return IsConstexpr() ? generated_type_ : "TNode<" + generated_type_ + ">";
}

std::string AbstractType::GetGeneratedTNodeTypeNameImpl() const {
  if (generated_type_.empty()) return parent()->GetGeneratedTNodeTypeName();
  return generated_type_;
}

std::vector<TypeChecker> AbstractType::GetTypeCheckers() const {
  if (UseParentTypeChecker()) return parent()->GetTypeCheckers();
  std::string type_name = name();
  if (auto strong_type =
          Type::MatchUnaryGeneric(this, TypeOracle::GetWeakGeneric())) {
    auto strong_runtime_types = (*strong_type)->GetTypeCheckers();
    std::vector<TypeChecker> result;
    for (const TypeChecker& type : strong_runtime_types) {
      // Generic parameter in Weak<T> should have already been checked to
      // extend HeapObject, so it couldn't itself be another weak type.
      DCHECK(type.weak_ref_to.empty());
      result.push_back({type_name, type.type});
    }
    return result;
  }
  return {{type_name, ""}};
}

std::string BuiltinPointerType::ToExplicitString() const {
  std::stringstream result;
  result << "builtin (";
  PrintCommaSeparatedList(result, parameter_types_);
  result << ") => " << *return_type_;
  return result.str();
}

std::string BuiltinPointerType::SimpleNameImpl() const {
  std::stringstream result;
  result << "BuiltinPointer";
  for (const Type* t : parameter_types_) {
    result << "_" << t->SimpleName();
  }
  result << "_" << return_type_->SimpleName();
  return result.str();
}

std::string UnionType::ToExplicitString() const {
  std::stringstream result;
  result << "(";
  bool first = true;
  for (const Type* t : types_) {
    if (!first) {
      result << " | ";
    }
    first = false;
    result << *t;
  }
  result << ")";
  return result.str();
}

std::string UnionType::SimpleNameImpl() const {
  std::stringstream result;
  bool first = true;
  for (const Type* t : types_) {
    if (!first) {
      result << "_OR_";
    }
    first = false;
    result << t->SimpleName();
  }
  return result.str();
}

std::string UnionType::GetGeneratedTNodeTypeNameImpl() const {
  if (types_.size() <= 3) {
    std::set<std::string> members;
    for (const Type* t : types_) {
      members.insert(t->GetGeneratedTNodeTypeName());
    }
    if (members == std::set<std::string>{"Smi", "HeapNumber"}) {
      return "Number";
    }
    if (members == std::set<std::string>{"Smi", "HeapNumber", "BigInt"}) {
      return "Numeric";
    }
  }
  return parent()->GetGeneratedTNodeTypeName();
}

std::string UnionType::GetRuntimeType() const {
  for (const Type* t : types_) {
    if (!t->IsSubtypeOf(TypeOracle::GetTaggedType())) {
      return parent()->GetRuntimeType();
    }
  }
  return "Tagged<" + GetConstexprGeneratedTypeName() + ">";
}

// static
void UnionType::InsertConstexprGeneratedTypeName(std::set<std::string>& names,
                                                 const Type* t) {
  if (t->IsUnionType()) {
    for (const Type* u : ((const UnionType*)t)->types_) {
      names.insert(u->GetConstexprGeneratedTypeName());
    }
  } else {
    names.insert(t->GetConstexprGeneratedTypeName());
  }
}

std::string UnionType::GetConstexprGeneratedTypeName() const {
  // For non-tagged unions, use the superclass GetConstexprGeneratedTypeName.
  for (const Type* t : types_) {
    if (!t->IsSubtypeOf(TypeOracle::GetTaggedType())) {
      return this->Type::GetConstexprGeneratedTypeName();
    }
  }

  // Allow some aliased simple names to be used as-is.
  std::string simple_name = SimpleName();
  if (simple_name == "Object") return simple_name;
  if (simple_name == "Number") return simple_name;
  if (simple_name == "Numeric") return simple_name;
  if (simple_name == "JSAny") return simple_name;
  if (simple_name == "JSPrimitive") return simple_name;

  // Deduplicate generated typenames and flatten unions.
  std::set<std::string> names;
  for (const Type* t : types_) {
    InsertConstexprGeneratedTypeName(names, t);
  }
  std::stringstream result;
  result << "Union<";
  bool first = true;
  for (std::string name : names) {
    if (!first) {
      result << ", ";
    }
    first = false;
    result << name;
  }
  result << ">";
  return result.str();
}

std::string UnionType::GetDebugType() const { return parent()->GetDebugType(); }

void UnionType::RecomputeParent() {
  const Type* parent = nullptr;
  for (const Type* t : types_) {
    if (parent == nullptr) {
      parent = t;
    } else {
      parent = CommonSupertype(parent, t);
    }
  }
  set_parent(parent);
}

void UnionType::Subtract(const Type* t) {
  for (auto it = types_.begin(); it != types_.end();) {
    if ((*it)->IsSubtypeOf(t)) {
      it = types_.erase(it);
    } else {
      ++it;
    }
  }
  if (types_.empty()) types_.insert(TypeOracle::GetNeverType());
  RecomputeParent();
}

const Type* SubtractType(const Type* a, const Type* b) {
  UnionType result = UnionType::FromType(a);
  result.Subtract(b);
  return TypeOracle::GetUnionType(result);
}

std::string BitFieldStructType::ToExplicitString() const {
  return "bitfield struct " + name();
}

const BitField& BitFieldStructType::LookupField(const std::string& name) const {
  for (const BitField& field : fields_) {
    if (field.name_and_type.name == name) {
      return field;
    }
  }
  ReportError("Couldn't find bitfield ", name);
}

void AggregateType::CheckForDuplicateFields() const {
  // Check the aggregate hierarchy and currently defined class for duplicate
  // field declarations.
  auto hierarchy = GetHierarchy();
  std::map<std::string, const AggregateType*> field_names;
  for (const AggregateType* aggregate_type : hierarchy) {
    for (const Field& field : aggregate_type->fields()) {
      const std::string& field_name = field.name_and_type.name;
      auto i = field_names.find(field_name);
      if (i != field_names.end()) {
        CurrentSourcePosition::Scope current_source_position(field.pos);
        std::string aggregate_type_name =
            aggregate_type->IsClassType() ? "class" : "struct";
        if (i->second == this) {
          ReportError(aggregate_type_name, " '", name(),
                      "' declares a field with the name '", field_name,
                      "' more than once");
        } else {
          ReportError(aggregate_type_name, " '", name(),
                      "' declares a field with the name '", field_name,
                      "' that masks an inherited field from class '",
                      i->second->name(), "'");
        }
      }
      field_names[field_name] = aggregate_type;
    }
  }
}

std::vector<const AggregateType*> AggregateType::GetHierarchy() const {
  if (!is_finalized_) Finalize();
  std::vector<const AggregateType*> hierarchy;
  const AggregateType* current_container_type = this;
  while (current_container_type != nullptr) {
    hierarchy.push_back(current_container_type);
    current_container_type =
        current_container_type->IsClassType()
            ? ClassType::cast(current_container_type)->GetSuperClass()
            : nullptr;
  }
  std::reverse(hierarchy.begin(), hierarchy.end());
  return hierarchy;
}

bool AggregateType::HasField(const std::string& name) const {
  if (!is_finalized_) Finalize();
  for (auto& field : fields_) {
    if (field.name_and_type.name == name) return true;
  }
  if (parent() != nullptr) {
    if (auto parent_class = ClassType::DynamicCast(parent())) {
      return parent_class->HasField(name);
    }
  }
  return false;
}

const Field& AggregateType::LookupFieldInternal(const std::string& name) const {
  for (auto& field : fields_) {
    if (field.name_and_type.name == name) return field;
  }
  if (parent() != nullptr) {
    if (auto parent_class = ClassType::DynamicCast(parent())) {
      return parent_class->LookupField(name);
    }
  }
  ReportError("no field ", name, " found in ", this->ToString());
}

const Field& AggregateType::LookupField(const std::string& name) const {
  if (!is_finalized_) Finalize();
  return LookupFieldInternal(name);
}

StructType::StructType(Namespace* nspace, const StructDeclaration* decl,
                       MaybeSpecializationKey specialized_from)
    : AggregateType(Kind::kStructType, nullptr, nspace, decl->name->value,
                    specialized_from),
      decl_(decl) {
  if (decl->flags & StructFlag::kExport) {
    generated_type_name_ = "TorqueStruct" + name();
  } else {
    generated_type_name_ =
        GlobalContext::MakeUniqueName("TorqueStruct" + SimpleName());
  }
}

std::string StructType::GetGeneratedTypeNameImpl() const {
  return generated_type_name_;
}

size_t StructType::PackedSize() const {
  size_t result = 0;
  for (const Field& field : fields()) {
    result += std::get<0>(field.GetFieldSizeInformation());
  }
  return result;
}

StructType::Classification StructType::ClassifyContents() const {
  Classification result = ClassificationFlag::kEmpty;
  for (const Field& struct_field : fields()) {
    const Type* field_type = struct_field.name_and_type.type;
    if (field_type->IsSubtypeOf(TypeOracle::GetStrongTaggedType())) {
      result |= ClassificationFlag::kStrongTagged;
    } else if (field_type->IsSubtypeOf(TypeOracle::GetTaggedType())) {
      result |= ClassificationFlag::kWeakTagged;
    } else if (auto field_as_struct = field_type->StructSupertype()) {
      result |= (*field_as_struct)->ClassifyContents();
    } else {
      result |= ClassificationFlag::kUntagged;
    }
  }
  return result;
}

// static
std::string Type::ComputeName(const std::string& basename,
                              MaybeSpecializationKey specialized_from) {
  if (!specialized_from) return basename;
  if (specialized_from->generic == TypeOracle::GetConstReferenceGeneric()) {
    return torque::ToString("const &", *specialized_from->specialized_types[0]);
  }
  if (specialized_from->generic == TypeOracle::GetMutableReferenceGeneric()) {
    return torque::ToString("&", *specialized_from->specialized_types[0]);
  }
  std::stringstream s;
  s << basename << "<";
  bool first = true;
  for (auto t : specialized_from->specialized_types) {
    if (!first) {
      s << ", ";
    }
    s << t->ToString();
    first = false;
  }
  s << ">";
  return s.str();
}

std::string StructType::SimpleNameImpl() const { return decl_->name->value; }

// static
std::optional<const Type*> Type::MatchUnaryGeneric(const Type* type,
                                                   GenericType* generic) {
  DCHECK_EQ(generic->generic_parameters().size(), 1);
  if (!type->GetSpecializedFrom()) {
    return std::nullopt;
  }
  auto& key = type->GetSpecializedFrom().value();
  if (key.generic != generic || key.specialized_types.size() != 1) {
    return std::nullopt;
  }
  return {key.specialized_types[0]};
}

std::vector<Method*> AggregateType::Methods(const std::string& name) const {
  if (!is_finalized_) Finalize();
  std::vector<Method*> result;
  std::copy_if(methods_.begin(), methods_.end(), std::back_inserter(result),
               [name](Macro* macro) { return macro->ReadableName() == name; });
  if (result.empty() && parent() != nullptr) {
    if (auto aggregate_parent = parent()->AggregateSupertype()) {
      return (*aggregate_parent)->Methods(name);
    }
  }
  return result;
}

std::string StructType::ToExplicitString() const { return "struct " + name(); }

void StructType::Finalize() const {
  if (is_finalized_) return;
  {
    CurrentScope::Scope scope_activator(nspace());
    CurrentSourcePosition::Scope position_activator(decl_->pos);
    TypeVisitor::VisitStructMethods(const_cast<StructType*>(this), decl_);
  }
  is_finalized_ = true;
  CheckForDuplicateFields();
}

ClassType::ClassType(const Type* parent, Namespace* nspace,
                     const std::string& name, ClassFlags flags,
                     const std::string& generates, const ClassDeclaration* decl,
                     const TypeAlias* alias)
    : AggregateType(Kind::kClassType, parent, nspace, name),
      size_(ResidueClass::Unknown()),
      flags_(flags),
      generates_(generates),
      decl_(decl),
      alias_(alias) {}

std::string ClassType::GetGeneratedTNodeTypeNameImpl() const {
  return generates_;
}

std::string ClassType::GetGeneratedTypeNameImpl() const {
  return IsConstexpr() ? GetGeneratedTNodeTypeName()
                       : "TNode<" + GetGeneratedTNodeTypeName() + ">";
}

std::string ClassType::ToExplicitString() const { return "class " + name(); }

bool ClassType::AllowInstantiation() const {
  return (!IsExtern() || nspace()->IsDefaultNamespace()) && !IsAbstract();
}

void ClassType::Finalize() const {
  if (is_finalized_) return;
  CurrentScope::Scope scope_activator(alias_->ParentScope());
  CurrentSourcePosition::Scope position_activator(decl_->pos);
  TypeVisitor::VisitClassFieldsAndMethods(const_cast<ClassType*>(this),
                                          this->decl_);
  is_finalized_ = true;
  CheckForDuplicateFields();
}

std::vector<Field> ClassType::ComputeAllFields() const {
  std::vector<Field> all_fields;
  const ClassType* super_class = this->GetSuperClass();
  if (super_class) {
    all_fields = super_class->ComputeAllFields();
  }
  const std::vector<Field>& fields = this->fields();
  all_fields.insert(all_fields.end(), fields.begin(), fields.end());
  return all_fields;
}

std::vector<Field> ClassType::ComputeHeaderFields() const {
  std::vector<Field> result;
  for (Field& field : ComputeAllFields()) {
    if (field.index) break;
    // The header is allowed to end with an optional padding field of size 0.
    DCHECK(std::get<0>(field.GetFieldSizeInformation()) == 0 ||
           *field.offset < header_size());
    result.push_back(std::move(field));
  }
  return result;
}

std::vector<Field> ClassType::ComputeArrayFields() const {
  std::vector<Field> result;
  for (Field& field : ComputeAllFields()) {
    if (!field.index) {
      // The header is allowed to end with an optional padding field of size 0.
      DCHECK(std::get<0>(field.GetFieldSizeInformation()) == 0 ||
             *field.offset < header_size());
      continue;
    }
    result.push_back(std::move(field));
  }
  return result;
}

void ClassType::InitializeInstanceTypes(
    std::optional<int> own, std::optional<std::pair<int, int>> range) const {
  DCHECK(!own_instance_type_.has_value());
  DCHECK(!instance_type_range_.has_value());
  own_instance_type_ = own;
  instance_type_range_ = range;
}

std::optional<int> ClassType::OwnInstanceType() const {
  DCHECK(GlobalContext::IsInstanceTypesInitialized());
  return own_instance_type_;
}

std::optional<std::pair<int, int>> ClassType::InstanceTypeRange() const {
  DCHECK(GlobalContext::IsInstanceTypesInitialized());
  return instance_type_range_;
}

namespace {
void ComputeSlotKindsHelper(std::vector<ObjectSlotKind>* slots,
                            size_t start_offset,
                            const std::vector<Field>& fields) {
  size_t offset = start_offset;
  for (const Field& field : fields) {
    size_t field_size = std::get<0>(field.GetFieldSizeInformation());
    // Support optional padding fields.
    if (field_size == 0) continue;
    size_t slot_index = offset / TargetArchitecture::TaggedSize();
    // Rounding-up division to find the number of slots occupied by all the
    // fields up to and including the current one.
    size_t used_slots =
        (offset + field_size + TargetArchitecture::TaggedSize() - 1) /
        TargetArchitecture::TaggedSize();
    while (used_slots > slots->size()) {
      slots->push_back(ObjectSlotKind::kNoPointer);
    }
    const Type* type = field.name_and_type.type;
    if (auto struct_type = type->StructSupertype()) {
      ComputeSlotKindsHelper(slots, offset, (*struct_type)->fields());
    } else {
      ObjectSlotKind kind;
      if (type->IsSubtypeOf(TypeOracle::GetObjectType())) {
        if (field.custom_weak_marking) {
          kind = ObjectSlotKind::kCustomWeakPointer;
        } else {
          kind = ObjectSlotKind::kStrongPointer;
        }
      } else if (type->IsSubtypeOf(TypeOracle::GetTaggedType())) {
        DCHECK(!field.custom_weak_marking);
        kind = ObjectSlotKind::kMaybeObjectPointer;
      } else {
        kind = ObjectSlotKind::kNoPointer;
      }
      DCHECK(slots->at(slot_index) == ObjectSlotKind::kNoPointer);
      slots->at(slot_index) = kind;
    }

    offset += field_size;
  }
}
}  // namespace

std::vector<ObjectSlotKind> ClassType::ComputeHeaderSlotKinds() const {
  std::vector<ObjectSlotKind> result;
  std::vector<Field> header_fields = ComputeHeaderFields();
  ComputeSlotKindsHelper(&result, 0, header_fields);
  DCHECK_EQ(std::ceil(static_cast<double>(header_size()) /
                      TargetArchitecture::TaggedSize()),
            result.size());
  return result;
}

std::optional<ObjectSlotKind> ClassType::ComputeArraySlotKind() const {
  std::vector<ObjectSlotKind> kinds;
  ComputeSlotKindsHelper(&kinds, 0, ComputeArrayFields());
  if (kinds.empty()) return std::nullopt;
  std::sort(kinds.begin(), kinds.end());
  if (kinds.front() == kinds.back()) return {kinds.front()};
  if (kinds.front() == ObjectSlotKind::kStrongPointer &&
      kinds.back() == ObjectSlotKind::kMaybeObjectPointer) {
    return ObjectSlotKind::kMaybeObjectPointer;
  }
  Error("Array fields mix types with different GC visitation requirements.")
      .Throw();
}

bool ClassType::HasNoPointerSlotsExceptMap() const {
  const auto header_slot_kinds = ComputeHeaderSlotKinds();
  DCHECK_GE(header_slot_kinds.size(), 1);
  DCHECK_EQ(ComputeHeaderFields()[0].name_and_type.type,
            TypeOracle::GetMapType());
  for (size_t i = 1; i < header_slot_kinds.size(); ++i) {
    if (header_slot_kinds[i] != ObjectSlotKind::kNoPointer) return false;
  }
  if (auto slot = ComputeArraySlotKind()) {
    if (*slot != ObjectSlotKind::kNoPointer) return false;
  }
  return true;
}

bool ClassType::HasIndexedFieldsIncludingInParents() const {
  for (const auto& field : fields_) {
    if (field.index.has_value()) return true;
  }
  if (const ClassType* parent = GetSuperClass()) {
    return parent->HasIndexedFieldsIncludingInParents();
  }
  return false;
}

const Field* ClassType::GetFieldPreceding(size_t field_index) const {
  if (field_index > 0) {
    return &fields_[field_index - 1];
  }
  if (const ClassType* parent = GetSuperClass()) {
    return parent->GetFieldPreceding(parent->fields_.size());
  }
  return nullptr;
}

const ClassType* ClassType::GetClassDeclaringField(const Field& f) const {
  for (const Field& field : fields_) {
    if (f.name_and_type.name == field.name_and_type.name) return this;
  }
  return GetSuperClass()->GetClassDeclaringField(f);
}

std::string ClassType::GetSliceMacroName(const Field& field) const {
  const ClassType* declarer = GetClassDeclaringField(field);
  return "FieldSlice" + declarer->name() +
         CamelifyString(field.name_and_type.name);
}

void ClassType::GenerateAccessors() {
  bool at_or_after_indexed_field = false;
  if (const ClassType* parent = GetSuperClass()) {
    at_or_after_indexed_field = parent->HasIndexedFieldsIncludingInParents();
  }
  // For each field, construct AST snippets that implement a CSA accessor
  // function. The implementation iterator will turn the snippets into code.
  for (size_t field_index = 0; field_index < fields_.size(); ++field_index) {
    Field& field = fields_[field_index];
    if (field.name_and_type.type == TypeOracle::GetVoidType()) {
      continue;
    }
    at_or_after_indexed_field =
        at_or_after_indexed_field || field.index.has_value();
    CurrentSourcePosition::Scope position_activator(field.pos);

    IdentifierExpression* parameter = MakeIdentifierExpression("o");
    IdentifierExpression* index = MakeIdentifierExpression("i");

    std::string camel_field_name = CamelifyString(field.name_and_type.name);

    if (at_or_after_indexed_field) {
      if (!field.index.has_value()) {
        // There's no fundamental reason we couldn't generate functions to get
        // references instead of slices, but it's not yet implemented.
        ReportError(
            "Torque doesn't yet support non-indexed fields after indexed "
            "fields");
      }

      GenerateSliceAccessor(field_index);
    }

    // For now, only generate indexed accessors for simple types
    if (field.index.has_value() && field.name_and_type.type->IsStructType()) {
      continue;
    }

    // An explicit index is only used for indexed fields not marked as optional.
    // Optional fields implicitly load or store item zero.
    bool use_index = field.index && !field.index->optional;

    // Load accessor
    std::string load_macro_name = "Load" + this->name() + camel_field_name;
    Signature load_signature;
    load_signature.parameter_names.push_back(MakeNode<Identifier>("o"));
    load_signature.parameter_types.types.push_back(this);
    if (use_index) {
      load_signature.parameter_names.push_back(MakeNode<Identifier>("i"));
      load_signature.parameter_types.types.push_back(
          TypeOracle::GetIntPtrType());
    }
    load_signature.parameter_types.var_args = false;
    load_signature.return_type = field.name_and_type.type;

    Expression* load_expression =
        MakeFieldAccessExpression(parameter, field.name_and_type.name);
    if (use_index) {
      load_expression =
          MakeNode<ElementAccessExpression>(load_expression, index);
    }
    Statement* load_body = MakeNode<ReturnStatement>(load_expression);
    Declarations::DeclareMacro(load_macro_name, true, std::nullopt,
                               load_signature, load_body, std::nullopt);

    // Store accessor
    if (!field.const_qualified) {
      IdentifierExpression* value = MakeIdentifierExpression("v");
      std::string store_macro_name = "Store" + this->name() + camel_field_name;
      Signature store_signature;
      store_signature.parameter_names.push_back(MakeNode<Identifier>("o"));
      store_signature.parameter_types.types.push_back(this);
      if (use_index) {
        store_signature.parameter_names.push_back(MakeNode<Identifier>("i"));
        store_signature.parameter_types.types.push_back(
            TypeOracle::GetIntPtrType());
      }
      store_signature.parameter_names.push_back(MakeNode<Identifier>("v"));
      store_signature.parameter_types.types.push_back(field.name_and_type.type);
      store_signature.parameter_types.var_args = false;
      // TODO(danno): Store macros probably should return their value argument
      store_signature.return_type = TypeOracle::GetVoidType();
      Expression* store_expression =
          MakeFieldAccessExpression(parameter, field.name_and_type.name);
      if (use_index) {
        store_expression =
            MakeNode<ElementAccessExpression>(store_expression, index);
      }
      Statement* store_body = MakeNode<ExpressionStatement>(
          MakeNode<AssignmentExpression>(store_expression, value));
      Declarations::DeclareMacro(store_macro_name, true, std::nullopt,
                                 store_signature, store_body, std::nullopt,
                                 false);
    }
  }
}

void ClassType::GenerateSliceAccessor(size_t field_index) {
  // Generate a Torque macro for getting a Slice to this field. This macro can
  // be called by the dot operator for this field. In Torque, this function for
  // class "ClassName" and field "field_name" and field type "FieldType" would
  // be written as one of the following:
  //
  // If the field has a known offset (in this example, 16):
  // FieldSliceClassNameFieldName(o: ClassName) {
  //   return torque_internal::unsafe::New{Const,Mutable}Slice<FieldType>(
  //     /*object:*/ o,
  //     /*offset:*/ 16,
  //     /*length:*/ torque_internal::%IndexedFieldLength<ClassName>(
  //                     o, "field_name")
  //   );
  // }
  //
  // If the field has an unknown offset, and the previous field is named p, is
  // not const, and is of type PType with size 4:
  // FieldSliceClassNameFieldName(o: ClassName) {
  //   const previous = %FieldSlice<ClassName, MutableSlice<PType>>(o, "p");
  //   return torque_internal::unsafe::New{Const,Mutable}Slice<FieldType>(
  //     /*object:*/ o,
  //     /*offset:*/ previous.offset + 4 * previous.length,
  //     /*length:*/ torque_internal::%IndexedFieldLength<ClassName>(
  //                     o, "field_name")
  //   );
  // }
  const Field& field = fields_[field_index];
  std::string macro_name = GetSliceMacroName(field);
  Signature signature;
  Identifier* parameter_identifier = MakeNode<Identifier>("o");
  signature.parameter_names.push_back(parameter_identifier);
  signature.parameter_types.types.push_back(this);
  signature.parameter_types.var_args = false;
  signature.return_type =
      field.const_qualified
          ? TypeOracle::GetConstSliceType(field.name_and_type.type)
          : TypeOracle::GetMutableSliceType(field.name_and_type.type);

  std::vector<Statement*> statements;
  Expression* offset_expression = nullptr;
  IdentifierExpression* parameter =
      MakeNode<IdentifierExpression>(parameter_identifier);

  if (field.offset.has_value()) {
    offset_expression =
        MakeNode<IntegerLiteralExpression>(IntegerLiteral(*field.offset));
  } else {
    const Field* previous = GetFieldPreceding(field_index);
    DCHECK_NOT_NULL(previous);

    const Type* previous_slice_type =
        previous->const_qualified
            ? TypeOracle::GetConstSliceType(previous->name_and_type.type)
            : TypeOracle::GetMutableSliceType(previous->name_and_type.type);

    // %FieldSlice<ClassName, MutableSlice<PType>>(o, "p")
    Expression* previous_expression = MakeCallExpression(
        MakeIdentifierExpression(
            {"torque_internal"}, "%FieldSlice",
            {MakeNode<PrecomputedTypeExpression>(this),
             MakeNode<PrecomputedTypeExpression>(previous_slice_type)}),
        {parameter, MakeNode<StringLiteralExpression>(
                        StringLiteralQuote(previous->name_and_type.name))});

    // const previous = %FieldSlice<ClassName, MutableSlice<PType>>(o, "p");
    Statement* define_previous =
        MakeConstDeclarationStatement("previous", previous_expression);
    statements.push_back(define_previous);

    // 4
    size_t previous_element_size;
    std::tie(previous_element_size, std::ignore) =
        *SizeOf(previous->name_and_type.type);
    Expression* previous_element_size_expression =
        MakeNode<IntegerLiteralExpression>(
            IntegerLiteral(previous_element_size));

    // previous.length
    Expression* previous_length_expression = MakeFieldAccessExpression(
        MakeIdentifierExpression("previous"), "length");

    // previous.offset
    Expression* previous_offset_expression = MakeFieldAccessExpression(
        MakeIdentifierExpression("previous"), "offset");

    // 4 * previous.length
    // In contrast to the code used for allocation, we don't need overflow
    // checks here because we already know all the offsets fit into memory.
    offset_expression = MakeCallExpression(
        "*", {previous_element_size_expression, previous_length_expression});

    // previous.offset + 4 * previous.length
    offset_expression = MakeCallExpression(
        "+", {previous_offset_expression, offset_expression});
  }

  // torque_internal::%IndexedFieldLength<ClassName>(o, "field_name")
  Expression* length_expression = MakeCallExpression(
      MakeIdentifierExpression({"torque_internal"}, "%IndexedFieldLength",
                               {MakeNode<PrecomputedTypeExpression>(this)}),
      {parameter, MakeNode<StringLiteralExpression>(
                      StringLiteralQuote(field.name_and_type.name))});

  // torque_internal::unsafe::New{Const,Mutable}Slice<FieldType>(
  //   /*object:*/ o,
  //   /*offset:*/ <<offset_expression>>,
  //   /*length:*/ torque_internal::%IndexedFieldLength<ClassName>(
  //                   o, "field_name")
  // )
  IdentifierExpression* new_struct = MakeIdentifierExpression(
      {"torque_internal", "unsafe"},
      field.const_qualified ? "NewConstSlice" : "NewMutableSlice",
      {MakeNode<PrecomputedTypeExpression>(field.name_and_type.type)});
  Expression* slice_expression = MakeCallExpression(
      new_struct, {parameter, offset_expression, length_expression});

  statements.push_back(MakeNode<ReturnStatement>(slice_expression));
  Statement* block =
      MakeNode<BlockStatement>(/*deferred=*/false, std::move(statements));

  Macro* macro = Declarations::DeclareMacro(macro_name, true, std::nullopt,
                                            signature, block, std::nullopt);
  if (this->ShouldGenerateCppObjectLayoutDefinitionAsserts()) {
    GlobalContext::EnsureInCCDebugOutputList(TorqueMacro::cast(macro),
                                             macro->Position().source);
  } else {
    GlobalContext::EnsureInCCOutputList(TorqueMacro::cast(macro),
                                        macro->Position().source);
  }
}

bool ClassType::HasStaticSize() const {
  if (IsSubtypeOf(TypeOracle::GetJSObjectType()) && !IsShape()) return false;
  return size().SingleValue().has_value();
}

SourceId ClassType::AttributedToFile() const {
  bool in_test_directory = StringStartsWith(
      SourceFileMap::PathFromV8Root(GetPosition().source).substr(), "test/");
  if (!in_test_directory && (IsExtern() || ShouldExport())) {
    return GetPosition().source;
  }
  return SourceFileMap::GetSourceId("src/objects/torque-defined-classes.tq");
}

void PrintSignature(std::ostream& os, const Signature& sig, bool with_names) {
  os << "(";
  for (size_t i = 0; i < sig.parameter_types.types.size(); ++i) {
    if (i == 0 && sig.implicit_count != 0) os << "implicit ";
    if (sig.implicit_count > 0 && sig.implicit_count == i) {
      os << ")(";
    } else {
      if (i > 0) os << ", ";
    }
    if (with_names && !sig.parameter_names.empty()) {
      if (i < sig.parameter_names.size()) {
        os << sig.parameter_names[i] << ": ";
      }
    }
    os << *sig.parameter_types.types[i];
  }
  if (sig.parameter_types.var_args) {
    if (!sig.parameter_names.empty()) os << ", ";
    os << "...";
  }
  os << ")";
  os << ": " << *sig.return_type;

  if (sig.labels.empty()) return;

  os << " labels ";
  for (size_t i = 0; i < sig.labels.size(); ++i) {
    if (i > 0) os << ", ";
    os << sig.labels[i].name;
    if (!sig.labels[i].types.empty()) os << "(" << sig.labels[i].types << ")";
  }
}

std::ostream& operator<<(std::ostream& os, const NameAndType& name_and_type) {
  os << name_and_type.name;
  os << ": ";
  os << *name_and_type.type;
  return os;
}

std::ostream& operator<<(std::ostream& os, const Field& field) {
  os << field.name_and_type;
  if (field.custom_weak_marking) {
    os << " (custom weak)";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const Signature& sig) {
  PrintSignature(os, sig, true);
  return os;
}

std::ostream& operator<<(std::ostream& os, const TypeVector& types) {
  PrintCommaSeparatedList(os, types);
  return os;
}

std::ostream& operator<<(std::ostream& os, const ParameterTypes& p) {
  PrintCommaSeparatedList(os, p.types);
  if (p.var_args) {
    if (!p.types.empty()) os << ", ";
    os << "...";
  }
  return os;
}

bool Signature::HasSameTypesAs(const Signature& other,
                               ParameterMode mode) const {
  auto compare_types = types();
  auto other_compare_types = other.types();
  if (mode == ParameterMode::kIgnoreImplicit) {
    compare_types = GetExplicitTypes();
    other_compare_types = other.GetExplicitTypes();
  }
  if (!(compare_types == other_compare_types &&
        parameter_types.var_args == other.parameter_types.var_args &&
        return_type == other.return_type)) {
    return false;
  }
  if (labels.size() != other.labels.size()) {
    return false;
  }
  size_t i = 0;
  for (const auto& l : labels) {
    if (l.types != other.labels[i++].types) {
      return false;
    }
  }
  return true;
}

namespace {
bool FirstTypeIsContext(const std::vector<const Type*> parameter_types) {
  return !parameter_types.empty() &&
         (parameter_types[0] == TypeOracle::GetContextType() ||
          parameter_types[0] == TypeOracle::GetNoContextType());
}
}  // namespace

bool Signature::HasContextParameter() const {
  return FirstTypeIsContext(types());
}

bool BuiltinPointerType::HasContextParameter() const {
  return FirstTypeIsContext(parameter_types());
}

bool IsAssignableFrom(const Type* to, const Type* from) {
  if (to == from) return true;
  if (from->IsSubtypeOf(to)) return true;
  return TypeOracle::ImplicitlyConvertableFrom(to, from).has_value();
}

bool operator<(const Type& a, const Type& b) { return a.id() < b.id(); }

VisitResult ProjectStructField(VisitResult structure,
                               const std::string& fieldname) {
  BottomOffset begin = structure.stack_range().begin();

  // Check constructor this super classes for fields.
  const StructType* type = *structure.type()->StructSupertype();
  auto& fields = type->fields();
  for (auto& field : fields) {
    BottomOffset end = begin + LoweredSlotCount(field.name_and_type.type);
    if (field.name_and_type.name == fieldname) {
      return VisitResult(field.name_and_type.type, StackRange{begin, end});
    }
    begin = end;
  }

  ReportError("struct '", type->name(), "' doesn't contain a field '",
              fieldname, "'");
}

namespace {
void AppendLoweredTypes(const Type* type, std::vector<const Type*>* result) {
  if (type->IsConstexpr()) return;
  if (type->IsVoidOrNever()) return;
  if (std::optional<const StructType*> s = type->StructSupertype()) {
    for (const Field& field : (*s)->fields()) {
      AppendLoweredTypes(field.name_and_type.type, result);
    }
  } else {
    result->push_back(type);
  }
}
}  // namespace

TypeVector LowerType(const Type* type) {
  TypeVector result;
  AppendLoweredTypes(type, &result);
  return result;
}

size_t LoweredSlotCount(const Type* type) { return LowerType(type).size(); }

TypeVector LowerParameterTypes(const TypeVector& parameters) {
  std::vector<const Type*> result;
  for (const Type* t : parameters) {
    AppendLoweredTypes(t, &result);
  }
  return result;
}

TypeVector LowerParameterTypes(const ParameterTypes& parameter_types,
                               size_t arg_count) {
  std::vector<const Type*> result = LowerParameterTypes(parameter_types.types);
  for (size_t i = parameter_types.types.size(); i < arg_count; ++i) {
    DCHECK(parameter_types.var_args);
    AppendLoweredTypes(TypeOracle::GetObjectType(), &result);
  }
  return result;
}

VisitResult VisitResult::NeverResult() {
  VisitResult result;
  result.type_ = TypeOracle::GetNeverType();
  return result;
}

VisitResult VisitResult::TopTypeResult(std::string top_reason,
                                       const Type* from_type) {
  VisitResult result;
  result.type_ = TypeOracle::GetTopType(std::move(top_reason), from_type);
  return result;
}

std::tuple<size_t, std::string> Field::GetFieldSizeInformation() const {
  auto optional = SizeOf(this->name_and_type.type);
  if (optional.has_value()) {
    return *optional;
  }
  Error("fields of type ", *name_and_type.type, " are not (yet) supported")
      .Position(pos)
      .Throw();
}

size_t Type::AlignmentLog2() const {
  if (parent()) return parent()->AlignmentLog2();
  return TargetArchitecture::TaggedSize();
}

size_t AbstractType::AlignmentLog2() const {
  size_t alignment;
  if (this == TypeOracle::GetTaggedType()) {
    alignment = TargetArchitecture::TaggedSize();
  } else if (this == TypeOracle::GetRawPtrType()) {
    alignment = TargetArchitecture::RawPtrSize();
  } else if (this == TypeOracle::GetExternalPointerType()) {
    alignment = TargetArchitecture::ExternalPointerSize();
  } else if (this == TypeOracle::GetCppHeapPointerType()) {
    alignment = TargetArchitecture::CppHeapPointerSize();
  } else if (this == TypeOracle::GetTrustedPointerType()) {
    alignment = TargetArchitecture::TrustedPointerSize();
  } else if (this == TypeOracle::GetProtectedPointerType()) {
    alignment = TargetArchitecture::ProtectedPointerSize();
  } else if (this == TypeOracle::GetVoidType()) {
    alignment = 1;
  } else if (this == TypeOracle::GetInt8Type()) {
    alignment = kUInt8Size;
  } else if (this == TypeOracle::GetUint8Type()) {
    alignment = kUInt8Size;
  } else if (this == TypeOracle::GetInt16Type()) {
    alignment = kUInt16Size;
  } else if (this == TypeOracle::GetUint16Type()) {
    alignment = kUInt16Size;
  } else if (this == TypeOracle::GetInt32Type()) {
    alignment = kInt32Size;
  } else if (this == TypeOracle::GetUint32Type()) {
    alignment = kInt32Size;
  } else if (this == TypeOracle::GetFloat64Type()) {
    alignment = kDoubleSize;
  } else if (this == TypeOracle::GetIntPtrType()) {
    alignment = TargetArchitecture::RawPtrSize();
  } else if (this == TypeOracle::GetUIntPtrType()) {
    alignment = TargetArchitecture::RawPtrSize();
  } else {
    return Type::AlignmentLog2();
  }
  alignment = std::min(alignment, TargetArchitecture::TaggedSize());
  return base::bits::WhichPowerOfTwo(alignment);
}

size_t StructType::AlignmentLog2() const {
  if (this == TypeOracle::GetFloat64OrHoleType()) {
    return TypeOracle::GetFloat64Type()->AlignmentLog2();
  }
  size_t alignment_log_2 = 0;
  for (const Field& field : fields()) {
    alignment_log_2 =
        std::max(alignment_log_2, field.name_and_type.type->AlignmentLog2());
  }
  return alignment_log_2;
}

void Field::ValidateAlignment(ResidueClass at_offset) const {
  const Type* type = name_and_type.type;
  std::optional<const StructType*> struct_type = type->StructSupertype();
  if (struct_type && struct_type != TypeOracle::GetFloat64OrHoleType()) {
    for (const Field& field : (*struct_type)->fields()) {
      field.ValidateAlignment(at_offset);
      size_t field_size = std::get<0>(field.GetFieldSizeInformation());
      at_offset += field_size;
    }
  } else {
    size_t alignment_log_2 = name_and_type.type->AlignmentLog2();
    if (at_offset.AlignmentLog2() < alignment_log_2) {
      Error("field ", name_and_type.name, " at offset ", at_offset, " is not ",
            size_t{1} << alignment_log_2, "-byte aligned.")
          .Position(pos);
    }
  }
}

std::optional<std::tuple<size_t, std::string>> SizeOf(const Type* type) {
  std::string size_string;
  size_t size;
  if (type->IsSubtypeOf(TypeOracle::GetTaggedType())) {
    size = TargetArchitecture::TaggedSize();
    size_string = "kTaggedSize";
  } else if (type->IsSubtypeOf(TypeOracle::GetRawPtrType())) {
    size = TargetArchitecture::RawPtrSize();
    size_string = "kSystemPointerSize";
  } else if (type->IsSubtypeOf(TypeOracle::GetExternalPointerType())) {
    size = TargetArchitecture::ExternalPointerSize();
    size_string = "kExternalPointerSlotSize";
  } else if (type->IsSubtypeOf(TypeOracle::GetCppHeapPointerType())) {
    size = TargetArchitecture::CppHeapPointerSize();
    size_string = "kCppHeapPointerSlotSize";
  } else if (type->IsSubtypeOf(TypeOracle::GetTrustedPointerType())) {
    size = TargetArchitecture::TrustedPointerSize();
    size_string = "kTrustedPointerSize";
  } else if (type->IsSubtypeOf(TypeOracle::GetProtectedPointerType())) {
    size = TargetArchitecture::ProtectedPointerSize();
    size_string = "kTaggedSize";
  } else if (type->IsSubtypeOf(TypeOracle::GetVoidType())) {
    size = 0;
    size_string = "0";
  } else if (type->IsSubtypeOf(TypeOracle::GetInt8Type())) {
    size = kUInt8Size;
    size_string = "kUInt8Size";
  } else if (type->IsSubtypeOf(TypeOracle::GetUint8Type())) {
    size = kUInt8Size;
    size_string = "kUInt8Size";
  } else if (type->IsSubtypeOf(TypeOracle::GetInt16Type())) {
    size = kUInt16Size;
    size_string = "kUInt16Size";
  } else if (type->IsSubtypeOf(TypeOracle::GetUint16Type())) {
    size = kUInt16Size;
    size_string = "kUInt16Size";
  } else if (type->IsSubtypeOf(TypeOracle::GetInt32Type())) {
    size = kInt32Size;
    size_string = "kInt32Size";
  } else if (type->IsSubtypeOf(TypeOracle::GetUint32Type())) {
    size = kInt32Size;
    size_string = "kInt32Size";
  } else if (type->IsSubtypeOf(TypeOracle::GetFloat64Type())) {
    size = kDoubleSize;
    size_string = "kDoubleSize";
  } else if (type->IsSubtypeOf(TypeOracle::GetIntPtrType())) {
    size = TargetArchitecture::RawPtrSize();
    size_string = "kIntptrSize";
  } else if (type->IsSubtypeOf(TypeOracle::GetUIntPtrType())) {
    size = TargetArchitecture::RawPtrSize();
    size_string = "kIntptrSize";
  } else if (auto struct_type = type->StructSupertype()) {
    if (type == TypeOracle::GetFloat64OrHoleType()) {
      size = kDoubleSize;
      size_string = "kDoubleSize";
    } else {
      size = (*struct_type)->PackedSize();
      size_string = std::to_string(size);
    }
  } else {
    return {};
  }
  return std::make_tuple(size, size_string);
}

bool IsAnyUnsignedInteger(const Type* type) {
  return type == TypeOracle::GetUint32Type() ||
         type == TypeOracle::GetUint31Type() ||
         type == TypeOracle::GetUint16Type() ||
         type == TypeOracle::GetUint8Type() ||
         type == TypeOracle::GetUIntPtrType();
}

bool IsAllowedAsBitField(const Type* type) {
  if (type->IsBitFieldStructType()) {
    // No nested bitfield structs for now. We could reconsider if there's a
    // compelling use case.
    return false;
  }
  // Any integer-ish type, including bools and enums which inherit from integer
  // types, are allowed. Note, however, that we always zero-extend during
  // decoding regardless of signedness.
  return IsPointerSizeIntegralType(type) || Is32BitIntegralType(type);
}

bool IsPointerSizeIntegralType(const Type* type) {
  return type->IsSubtypeOf(TypeOracle::GetUIntPtrType()) ||
         type->IsSubtypeOf(TypeOracle::GetIntPtrType());
}

bool Is32BitIntegralType(const Type* type) {
  return type->IsSubtypeOf(TypeOracle::GetUint32Type()) ||
         type->IsSubtypeOf(TypeOracle::GetInt32Type()) ||
         type->IsSubtypeOf(TypeOracle::GetBoolType());
}

std::optional<NameAndType> ExtractSimpleFieldArraySize(
    const ClassType& class_type, Expression* array_size) {
  IdentifierExpression* identifier =
      IdentifierExpression::DynamicCast(array_size);
  if (!identifier || !identifier->generic_arguments.empty() ||
      !identifier->namespace_qualification.empty())
    return {};
  if (!class_type.HasField(identifier->name->value)) return {};
  return class_type.LookupField(identifier->name->value).name_and_type;
}

std::string Type::GetRuntimeType() const {
  if (IsSubtypeOf(TypeOracle::GetSmiType())) return "Tagged<Smi>";
  if (IsSubtypeOf(TypeOracle::GetTaggedType())) {
    return "Tagged<" + GetGeneratedTNodeTypeName() + ">";
  }
  if (std::optional<const StructType*> struct_type = StructSupertype()) {
    std::stringstream result;
    result << "std::tuple<";
    bool first = true;
    for (const Type* field_type : LowerType(*struct_type)) {
      if (!first) result << ", ";
      first = false;
      result << field_type->GetRuntimeType();
    }
    result << ">";
    return result.str();
  }
  return ConstexprVersion()->GetGeneratedTypeName();
}

std::string Type::GetDebugType() const {
  if (IsSubtypeOf(TypeOracle::GetSmiType())) return "uintptr_t";
  if (IsSubtypeOf(TypeOracle::GetTaggedType())) {
    return "uintptr_t";
  }
  if (std::optional<const StructType*> struct_type = StructSupertype()) {
    std::stringstream result;
    result << "std::tuple<";
    bool first = true;
    for (const Type* field_type : LowerType(*struct_type)) {
      if (!first) result << ", ";
      first = false;
      result << field_type->GetDebugType();
    }
    result << ">";
    return result.str();
  }
  return ConstexprVersion()->GetGeneratedTypeName();
}

}  // namespace v8::internal::torque
