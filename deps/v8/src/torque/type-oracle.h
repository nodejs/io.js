// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_TYPE_ORACLE_H_
#define V8_TORQUE_TYPE_ORACLE_H_

#include <memory>

#include "src/base/contextual.h"
#include "src/torque/declarable.h"
#include "src/torque/declarations.h"
#include "src/torque/types.h"
#include "src/torque/utils.h"

namespace v8 {
namespace internal {
namespace torque {

class TypeOracle : public base::ContextualClass<TypeOracle> {
 public:
  static const AbstractType* GetAbstractType(
      const Type* parent, std::string name, AbstractTypeFlags flags,
      std::string generated, const Type* non_constexpr_version,
      MaybeSpecializationKey specialized_from) {
    auto ptr = std::unique_ptr<AbstractType>(
        new AbstractType(parent, flags, std::move(name), std::move(generated),
                         non_constexpr_version, specialized_from));
    const AbstractType* result = ptr.get();
    if (non_constexpr_version) {
      DCHECK(ptr->IsConstexpr());
      non_constexpr_version->SetConstexprVersion(result);
    }
    Get().nominal_types_.push_back(std::move(ptr));
    return result;
  }

  static StructType* GetStructType(const StructDeclaration* decl,
                                   MaybeSpecializationKey specialized_from) {
    auto ptr = std::unique_ptr<StructType>(
        new StructType(CurrentNamespace(), decl, specialized_from));
    StructType* result = ptr.get();
    Get().aggregate_types_.push_back(std::move(ptr));
    return result;
  }

  static BitFieldStructType* GetBitFieldStructType(
      const Type* parent, const BitFieldStructDeclaration* decl) {
    auto ptr = std::unique_ptr<BitFieldStructType>(
        new BitFieldStructType(CurrentNamespace(), parent, decl));
    BitFieldStructType* result = ptr.get();
    Get().bit_field_struct_types_.push_back(std::move(ptr));
    return result;
  }

  static ClassType* GetClassType(const Type* parent, const std::string& name,
                                 ClassFlags flags, const std::string& generates,
                                 ClassDeclaration* decl,
                                 const TypeAlias* alias) {
    std::unique_ptr<ClassType> type(new ClassType(
        parent, CurrentNamespace(), name, flags, generates, decl, alias));
    ClassType* result = type.get();
    Get().aggregate_types_.push_back(std::move(type));
    return result;
  }

  static const BuiltinPointerType* GetBuiltinPointerType(
      TypeVector argument_types, const Type* return_type) {
    TypeOracle& self = Get();
    const Type* builtin_type = self.GetBuiltinType(BUILTIN_POINTER_TYPE_STRING);
    const BuiltinPointerType* result = self.function_pointer_types_.Add(
        BuiltinPointerType(builtin_type, argument_types, return_type,
                           self.all_builtin_pointer_types_.size()));
    if (result->function_pointer_type_id() ==
        self.all_builtin_pointer_types_.size()) {
      self.all_builtin_pointer_types_.push_back(result);
    }
    return result;
  }

  static const Type* GetGenericTypeInstance(GenericType* generic_type,
                                            TypeVector arg_types);

  static GenericType* GetReferenceGeneric(bool is_const) {
    return Declarations::LookupUniqueGenericType(
        QualifiedName({TORQUE_INTERNAL_NAMESPACE_STRING},
                      is_const ? CONST_REFERENCE_TYPE_STRING
                               : MUTABLE_REFERENCE_TYPE_STRING));
  }
  static GenericType* GetConstReferenceGeneric() {
    return GetReferenceGeneric(true);
  }
  static GenericType* GetMutableReferenceGeneric() {
    return GetReferenceGeneric(false);
  }

  static base::Optional<const Type*> MatchReferenceGeneric(
      const Type* reference_type, bool* is_const = nullptr);

  static GenericType* GetMutableSliceGeneric() {
    return Declarations::LookupUniqueGenericType(
        QualifiedName(MUTABLE_SLICE_TYPE_STRING));
  }
  static GenericType* GetConstSliceGeneric() {
    return Declarations::LookupUniqueGenericType(
        QualifiedName(CONST_SLICE_TYPE_STRING));
  }

  static GenericType* GetWeakGeneric() {
    return Declarations::LookupGlobalUniqueGenericType(WEAK_TYPE_STRING);
  }

  static GenericType* GetSmiTaggedGeneric() {
    return Declarations::LookupGlobalUniqueGenericType(SMI_TAGGED_TYPE_STRING);
  }

  static GenericType* GetLazyGeneric() {
    return Declarations::LookupGlobalUniqueGenericType(LAZY_TYPE_STRING);
  }

  static const Type* GetReferenceType(const Type* referenced_type,
                                      bool is_const) {
    return GetGenericTypeInstance(GetReferenceGeneric(is_const),
                                  {referenced_type});
  }
  static const Type* GetConstReferenceType(const Type* referenced_type) {
    return GetReferenceType(referenced_type, true);
  }
  static const Type* GetMutableReferenceType(const Type* referenced_type) {
    return GetReferenceType(referenced_type, false);
  }

  static const Type* GetMutableSliceType(const Type* referenced_type) {
    return GetGenericTypeInstance(GetMutableSliceGeneric(), {referenced_type});
  }
  static const Type* GetConstSliceType(const Type* referenced_type) {
    return GetGenericTypeInstance(GetConstSliceGeneric(), {referenced_type});
  }

  static const std::vector<const BuiltinPointerType*>&
  AllBuiltinPointerTypes() {
    return Get().all_builtin_pointer_types_;
  }

  static const Type* GetUnionType(UnionType type) {
    if (base::Optional<const Type*> single = type.GetSingleMember()) {
      return *single;
    }
    return Get().union_types_.Add(std::move(type));
  }

  static const Type* GetUnionType(const Type* a, const Type* b) {
    if (a->IsSubtypeOf(b)) return b;
    if (b->IsSubtypeOf(a)) return a;
    UnionType result = UnionType::FromType(a);
    result.Extend(b);
    return GetUnionType(std::move(result));
  }

  static const TopType* GetTopType(std::string reason,
                                   const Type* source_type) {
    std::unique_ptr<TopType> type(new TopType(std::move(reason), source_type));
    TopType* result = type.get();
    Get().top_types_.push_back(std::move(type));
    return result;
  }

  static const Type* GetArgumentsType() {
    return Get().GetBuiltinType(ARGUMENTS_TYPE_STRING);
  }

  static const Type* GetBoolType() {
    return Get().GetBuiltinType(BOOL_TYPE_STRING);
  }

  static const Type* GetConstexprBoolType() {
    return Get().GetBuiltinType(CONSTEXPR_BOOL_TYPE_STRING);
  }

  static const Type* GetConstexprStringType() {
    return Get().GetBuiltinType(CONSTEXPR_STRING_TYPE_STRING);
  }

  static const Type* GetConstexprIntPtrType() {
    return Get().GetBuiltinType(CONSTEXPR_INTPTR_TYPE_STRING);
  }

  static const Type* GetConstexprInstanceTypeType() {
    return Get().GetBuiltinType(CONSTEXPR_INSTANCE_TYPE_TYPE_STRING);
  }

  static const Type* GetVoidType() {
    return Get().GetBuiltinType(VOID_TYPE_STRING);
  }

  static const Type* GetRawPtrType() {
    return Get().GetBuiltinType(RAWPTR_TYPE_STRING);
  }

  static const Type* GetExternalPointerType() {
    return Get().GetBuiltinType(EXTERNALPTR_TYPE_STRING);
  }

  static const Type* GetIndirectPointerType() {
    return Get().GetBuiltinType(INDIRECTPTR_TYPE_STRING);
  }

  static const Type* GetProtectedPointerType() {
    return Get().GetBuiltinType(PROTECTEDPTR_TYPE_STRING);
  }

  static const Type* GetMapType() {
    return Get().GetBuiltinType(MAP_TYPE_STRING);
  }

  static const Type* GetObjectType() {
    return Get().GetBuiltinType(OBJECT_TYPE_STRING);
  }

  static const Type* GetHeapObjectType() {
    return Get().GetBuiltinType(HEAP_OBJECT_TYPE_STRING);
  }

  static const Type* GetTaggedZeroPatternType() {
    return Get().GetBuiltinType(TAGGED_ZERO_PATTERN_TYPE_STRING);
  }

  static const Type* GetJSAnyType() {
    return Get().GetBuiltinType(JSANY_TYPE_STRING);
  }

  static const Type* GetJSObjectType() {
    return Get().GetBuiltinType(JSOBJECT_TYPE_STRING);
  }

  static const Type* GetTaggedType() {
    return Get().GetBuiltinType(TAGGED_TYPE_STRING);
  }

  static const Type* GetStrongTaggedType() {
    return Get().GetBuiltinType(STRONG_TAGGED_TYPE_STRING);
  }

  static const Type* GetUninitializedType() {
    return Get().GetBuiltinType(UNINITIALIZED_TYPE_STRING);
  }

  static const Type* GetUninitializedHeapObjectType() {
    return Get().GetBuiltinType(
        QualifiedName({TORQUE_INTERNAL_NAMESPACE_STRING},
                      UNINITIALIZED_HEAP_OBJECT_TYPE_STRING));
  }

  static const Type* GetSmiType() {
    return Get().GetBuiltinType(SMI_TYPE_STRING);
  }

  static const Type* GetConstStringType() {
    return Get().GetBuiltinType(CONST_STRING_TYPE_STRING);
  }

  static const Type* GetStringType() {
    return Get().GetBuiltinType(STRING_TYPE_STRING);
  }

  static const Type* GetNumberType() {
    return Get().GetBuiltinType(NUMBER_TYPE_STRING);
  }

  static const Type* GetIntPtrType() {
    return Get().GetBuiltinType(INTPTR_TYPE_STRING);
  }

  static const Type* GetUIntPtrType() {
    return Get().GetBuiltinType(UINTPTR_TYPE_STRING);
  }

  static const Type* GetInt64Type() {
    return Get().GetBuiltinType(INT64_TYPE_STRING);
  }

  static const Type* GetUint64Type() {
    return Get().GetBuiltinType(UINT64_TYPE_STRING);
  }

  static const Type* GetInt32Type() {
    return Get().GetBuiltinType(INT32_TYPE_STRING);
  }

  static const Type* GetUint32Type() {
    return Get().GetBuiltinType(UINT32_TYPE_STRING);
  }

  static const Type* GetUint31Type() {
    return Get().GetBuiltinType(UINT31_TYPE_STRING);
  }

  static const Type* GetInt16Type() {
    return Get().GetBuiltinType(INT16_TYPE_STRING);
  }

  static const Type* GetUint16Type() {
    return Get().GetBuiltinType(UINT16_TYPE_STRING);
  }

  static const Type* GetInt8Type() {
    return Get().GetBuiltinType(INT8_TYPE_STRING);
  }

  static const Type* GetUint8Type() {
    return Get().GetBuiltinType(UINT8_TYPE_STRING);
  }

  static const Type* GetFloat64Type() {
    return Get().GetBuiltinType(FLOAT64_TYPE_STRING);
  }

  static const Type* GetFloat64OrHoleType() {
    return Get().GetBuiltinType(FLOAT64_OR_HOLE_TYPE_STRING);
  }

  static const Type* GetConstFloat64Type() {
    return Get().GetBuiltinType(CONST_FLOAT64_TYPE_STRING);
  }

  static const Type* GetIntegerLiteralType() {
    return Get().GetBuiltinType(INTEGER_LITERAL_TYPE_STRING);
  }

  static const Type* GetNeverType() {
    return Get().GetBuiltinType(NEVER_TYPE_STRING);
  }

  static const Type* GetConstInt31Type() {
    return Get().GetBuiltinType(CONST_INT31_TYPE_STRING);
  }

  static const Type* GetConstInt32Type() {
    return Get().GetBuiltinType(CONST_INT32_TYPE_STRING);
  }

  static const Type* GetContextType() {
    return Get().GetBuiltinType(CONTEXT_TYPE_STRING);
  }

  static const Type* GetNoContextType() {
    return Get().GetBuiltinType(NO_CONTEXT_TYPE_STRING);
  }

  static const Type* GetNativeContextType() {
    return Get().GetBuiltinType(NATIVE_CONTEXT_TYPE_STRING);
  }

  static const Type* GetJSFunctionType() {
    return Get().GetBuiltinType(JS_FUNCTION_TYPE_STRING);
  }

  static const Type* GetUninitializedIteratorType() {
    return Get().GetBuiltinType(UNINITIALIZED_ITERATOR_TYPE_STRING);
  }

  static const Type* GetFixedArrayBaseType() {
    return Get().GetBuiltinType(FIXED_ARRAY_BASE_TYPE_STRING);
  }

  static base::Optional<const Type*> ImplicitlyConvertableFrom(
      const Type* to, const Type* from) {
    while (from != nullptr) {
      for (GenericCallable* from_constexpr :
           Declarations::LookupGeneric(kFromConstexprMacroName)) {
        if (base::Optional<const Callable*> specialization =
                from_constexpr->GetSpecialization({to, from})) {
          if ((*specialization)->signature().GetExplicitTypes() ==
              TypeVector{from}) {
            return from;
          }
        }
      }
      from = from->parent();
    }
    return base::nullopt;
  }

  static const std::vector<std::unique_ptr<AggregateType>>& GetAggregateTypes();
  static const std::vector<std::unique_ptr<BitFieldStructType>>&
  GetBitFieldStructTypes();

  // By construction, this list of all classes is topologically sorted w.r.t.
  // inheritance.
  static std::vector<const ClassType*> GetClasses();

  static void FinalizeAggregateTypes();

  static size_t FreshTypeId() { return Get().next_type_id_++; }

  static Namespace* CreateGenericTypeInstantiationNamespace();

 private:
  const Type* GetBuiltinType(const QualifiedName& name) {
    return Declarations::LookupGlobalType(name);
  }
  const Type* GetBuiltinType(const std::string& name) {
    return GetBuiltinType(QualifiedName(name));
  }

  Deduplicator<BuiltinPointerType> function_pointer_types_;
  std::vector<const BuiltinPointerType*> all_builtin_pointer_types_;
  Deduplicator<UnionType> union_types_;
  std::vector<std::unique_ptr<Type>> nominal_types_;
  std::vector<std::unique_ptr<AggregateType>> aggregate_types_;
  std::vector<std::unique_ptr<BitFieldStructType>> bit_field_struct_types_;
  std::vector<std::unique_ptr<Type>> top_types_;
  std::vector<std::unique_ptr<Namespace>>
      generic_type_instantiation_namespaces_;
  size_t next_type_id_ = 0;
};

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_TYPE_ORACLE_H_
