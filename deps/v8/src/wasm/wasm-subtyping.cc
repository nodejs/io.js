// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-subtyping.h"

#include "src/wasm/canonical-types.h"
#include "src/wasm/wasm-module.h"

namespace v8::internal::wasm {

namespace {

V8_INLINE bool EquivalentIndices(ModuleTypeIndex index1, ModuleTypeIndex index2,
                                 const WasmModule* module1,
                                 const WasmModule* module2) {
  DCHECK(index1 != index2 || module1 != module2);
  return module1->canonical_type_id(index1) ==
         module2->canonical_type_id(index2);
}

bool ValidStructSubtypeDefinition(ModuleTypeIndex subtype_index,
                                  ModuleTypeIndex supertype_index,
                                  const WasmModule* sub_module,
                                  const WasmModule* super_module) {
  const StructType* sub_struct = sub_module->type(subtype_index).struct_type;
  const StructType* super_struct =
      super_module->type(supertype_index).struct_type;

  if (sub_struct->field_count() < super_struct->field_count()) {
    return false;
  }

  for (uint32_t i = 0; i < super_struct->field_count(); i++) {
    bool sub_mut = sub_struct->mutability(i);
    bool super_mut = super_struct->mutability(i);
    if (sub_mut != super_mut ||
        (sub_mut &&
         !EquivalentTypes(sub_struct->field(i), super_struct->field(i),
                          sub_module, super_module)) ||
        (!sub_mut && !IsSubtypeOf(sub_struct->field(i), super_struct->field(i),
                                  sub_module, super_module))) {
      return false;
    }
  }
  return true;
}

bool ValidArraySubtypeDefinition(ModuleTypeIndex subtype_index,
                                 ModuleTypeIndex supertype_index,
                                 const WasmModule* sub_module,
                                 const WasmModule* super_module) {
  const ArrayType* sub_array = sub_module->type(subtype_index).array_type;
  const ArrayType* super_array = super_module->type(supertype_index).array_type;
  bool sub_mut = sub_array->mutability();
  bool super_mut = super_array->mutability();

  return (sub_mut && super_mut &&
          EquivalentTypes(sub_array->element_type(),
                          super_array->element_type(), sub_module,
                          super_module)) ||
         (!sub_mut && !super_mut &&
          IsSubtypeOf(sub_array->element_type(), super_array->element_type(),
                      sub_module, super_module));
}

bool ValidFunctionSubtypeDefinition(ModuleTypeIndex subtype_index,
                                    ModuleTypeIndex supertype_index,
                                    const WasmModule* sub_module,
                                    const WasmModule* super_module) {
  const FunctionSig* sub_func = sub_module->type(subtype_index).function_sig;
  const FunctionSig* super_func =
      super_module->type(supertype_index).function_sig;

  if (sub_func->parameter_count() != super_func->parameter_count() ||
      sub_func->return_count() != super_func->return_count()) {
    return false;
  }

  for (uint32_t i = 0; i < sub_func->parameter_count(); i++) {
    // Contravariance for params.
    if (!IsSubtypeOf(super_func->parameters()[i], sub_func->parameters()[i],
                     super_module, sub_module)) {
      return false;
    }
  }
  for (uint32_t i = 0; i < sub_func->return_count(); i++) {
    // Covariance for returns.
    if (!IsSubtypeOf(sub_func->returns()[i], super_func->returns()[i],
                     sub_module, super_module)) {
      return false;
    }
  }

  return true;
}

HeapType::Representation NullSentinelImpl(HeapType type,
                                          const WasmModule* module) {
  switch (type.representation()) {
    case HeapType::kI31:
    case HeapType::kNone:
    case HeapType::kEq:
    case HeapType::kStruct:
    case HeapType::kArray:
    case HeapType::kAny:
    case HeapType::kString:
    case HeapType::kStringViewWtf8:
    case HeapType::kStringViewWtf16:
    case HeapType::kStringViewIter:
      return HeapType::kNone;
    case HeapType::kExtern:
    case HeapType::kNoExtern:
    case HeapType::kExternString:
      return HeapType::kNoExtern;
    case HeapType::kExn:
    case HeapType::kNoExn:
      return HeapType::kNoExn;
    case HeapType::kFunc:
    case HeapType::kNoFunc:
      return HeapType::kNoFunc;
    case HeapType::kI31Shared:
    case HeapType::kNoneShared:
    case HeapType::kEqShared:
    case HeapType::kStructShared:
    case HeapType::kArrayShared:
    case HeapType::kAnyShared:
    case HeapType::kStringShared:
    case HeapType::kStringViewWtf8Shared:
    case HeapType::kStringViewWtf16Shared:
    case HeapType::kStringViewIterShared:
      return HeapType::kNoneShared;
    case HeapType::kExternShared:
    case HeapType::kNoExternShared:
    case HeapType::kExternStringShared:
      return HeapType::kNoExternShared;
    case HeapType::kExnShared:
    case HeapType::kNoExnShared:
      return HeapType::kNoExnShared;
    case HeapType::kFuncShared:
    case HeapType::kNoFuncShared:
      return HeapType::kNoFuncShared;
    default: {
      bool is_shared = module->type(type.ref_index()).is_shared;
      return module->has_signature(type.ref_index())
                 ? (is_shared ? HeapType::kNoFuncShared : HeapType::kNoFunc)
                 : (is_shared ? HeapType::kNoneShared : HeapType::kNone);
    }
  }
}

bool IsNullSentinel(HeapType type) {
  switch (type.representation()) {
    case HeapType::kNone:
    case HeapType::kNoExtern:
    case HeapType::kNoFunc:
    case HeapType::kNoExn:
    case HeapType::kNoneShared:
    case HeapType::kNoExternShared:
    case HeapType::kNoFuncShared:
    case HeapType::kNoExnShared:
      return true;
    default:
      return false;
  }
}

}  // namespace

bool ValidSubtypeDefinition(ModuleTypeIndex subtype_index,
                            ModuleTypeIndex supertype_index,
                            const WasmModule* sub_module,
                            const WasmModule* super_module) {
  const TypeDefinition& subtype = sub_module->type(subtype_index);
  const TypeDefinition& supertype = super_module->type(supertype_index);
  if (subtype.kind != supertype.kind) return false;
  if (supertype.is_final) return false;
  if (subtype.is_shared != supertype.is_shared) return false;
  switch (subtype.kind) {
    case TypeDefinition::kFunction:
      return ValidFunctionSubtypeDefinition(subtype_index, supertype_index,
                                            sub_module, super_module);
    case TypeDefinition::kStruct:
      return ValidStructSubtypeDefinition(subtype_index, supertype_index,
                                          sub_module, super_module);
    case TypeDefinition::kArray:
      return ValidArraySubtypeDefinition(subtype_index, supertype_index,
                                         sub_module, super_module);
  }
}

namespace {
bool IsShared(HeapType type, const WasmModule* module) {
  return type.is_abstract_shared() ||
         (type.is_index() && module->type(type.ref_index()).is_shared);
}

HeapType::Representation MaybeShared(HeapType::Representation base,
                                     bool shared) {
  DCHECK(HeapType(base).is_abstract_non_shared());
  if (!shared) return base;
  switch (base) {
    case HeapType::kFunc:
      return HeapType::kFuncShared;
    case HeapType::kEq:
      return HeapType::kEqShared;
    case HeapType::kI31:
      return HeapType::kI31Shared;
    case HeapType::kStruct:
      return HeapType::kStructShared;
    case HeapType::kArray:
      return HeapType::kArrayShared;
    case HeapType::kAny:
      return HeapType::kAnyShared;
    case HeapType::kExtern:
      return HeapType::kExternShared;
    case HeapType::kExternString:
      return HeapType::kExternStringShared;
    case HeapType::kExn:
      return HeapType::kExnShared;
    case HeapType::kString:
      return HeapType::kStringShared;
    case HeapType::kStringViewWtf8:
      return HeapType::kStringViewWtf8Shared;
    case HeapType::kStringViewWtf16:
      return HeapType::kStringViewWtf16Shared;
    case HeapType::kStringViewIter:
      return HeapType::kStringViewIterShared;
    case HeapType::kNone:
      return HeapType::kNoneShared;
    case HeapType::kNoFunc:
      return HeapType::kNoFuncShared;
    case HeapType::kNoExtern:
      return HeapType::kNoExternShared;
    case HeapType::kNoExn:
      return HeapType::kNoExnShared;
    default:
      UNREACHABLE();
  }
}

// Same as WasmModule::canonical_type, but robust to {module == nullptr}, which
// is okay when {type} isn't an indexed type.
CanonicalValueType Canonicalize(ValueType type, const WasmModule* module) {
  if (!type.has_index()) return CanonicalValueType{type};
  DCHECK_NOT_NULL(module);
  return CanonicalValueType::FromIndex(
      type.kind(), module->canonical_type_id(type.ref_index()));
}
}  // namespace

V8_EXPORT_PRIVATE bool IsShared(ValueType type, const WasmModule* module) {
  switch (type.kind()) {
    case kRef:
    case kRefNull:
      return IsShared(type.heap_type(), module);
    default:
      return true;
  }
}

V8_NOINLINE V8_EXPORT_PRIVATE bool IsSubtypeOfImpl(
    ValueType subtype, ValueType supertype, const WasmModule* sub_module,
    const WasmModule* super_module) {
  DCHECK(subtype != supertype || sub_module != super_module);

  // The top type is the super type of all other types.
  if (supertype.kind() == kTop) return true;

  switch (subtype.kind()) {
    case kI32:
    case kI64:
    case kF16:
    case kF32:
    case kF64:
    case kS128:
    case kI8:
    case kI16:
    case kVoid:
    case kTop:
      return subtype == supertype;
    case kBottom:
      // The bottom type is a subtype of all types.
      return true;
    case kRtt:
      return supertype.kind() == kRtt &&
             EquivalentIndices(subtype.ref_index(), supertype.ref_index(),
                               sub_module, super_module);
    case kRef:
    case kRefNull:
      break;
  }

  DCHECK(subtype.is_object_reference());

  bool compatible_references = subtype.is_nullable()
                                   ? supertype.is_nullable()
                                   : supertype.is_object_reference();
  if (!compatible_references) return false;

  DCHECK(supertype.is_object_reference());

  // Now check that sub_heap and super_heap are subtype-related.
  return GetTypeCanonicalizer()->IsHeapSubtype(
      Canonicalize(subtype, sub_module), Canonicalize(supertype, super_module));
}

V8_NOINLINE V8_EXPORT_PRIVATE bool IsSubtypeOfImpl(
    CanonicalValueType subtype, CanonicalValueType supertype) {
  DCHECK_NE(subtype, supertype);  // Caller has checked.
  if (supertype.kind() == kTop) return true;
  if (subtype.kind() == kBottom) return true;
  // For all other kinds, the types would have needed to be equal.
  if (!subtype.is_object_reference()) return false;
  if (!supertype.is_object_reference()) return false;

  if (subtype.is_nullable() && !supertype.is_nullable()) return false;
  return GetTypeCanonicalizer()->IsHeapSubtype(subtype, supertype);
}

V8_NOINLINE V8_EXPORT_PRIVATE bool IsHeapSubtypeOfImpl(
    HeapType sub_heap, HeapType super_heap, const WasmModule* sub_module,
    const WasmModule* super_module) {
  CanonicalValueType sub_canon =
      Canonicalize(ValueType::Ref(sub_heap), sub_module);
  CanonicalValueType super_canon =
      Canonicalize(ValueType::Ref(super_heap), super_module);
  return GetTypeCanonicalizer()->IsHeapSubtype(sub_canon, super_canon);
}

V8_NOINLINE bool EquivalentTypes(ValueType type1, ValueType type2,
                                 const WasmModule* module1,
                                 const WasmModule* module2) {
  if (type1 == type2 && module1 == module2) return true;
  if (!type1.has_index() || !type2.has_index()) return type1 == type2;
  if (type1.kind() != type2.kind()) return false;

  DCHECK(type1 != type2 || module1 != module2);
  DCHECK(type1.has_index() && module1->has_type(type1.ref_index()) &&
         type2.has_index() && module2->has_type(type2.ref_index()));

  return EquivalentIndices(type1.ref_index(), type2.ref_index(), module1,
                           module2);
}

namespace {
// Returns the least common ancestor of two type indices, as a type index in
// {module1}.
HeapType::Representation CommonAncestor(ModuleTypeIndex type_index1,
                                        ModuleTypeIndex type_index2,
                                        const WasmModule* module1,
                                        const WasmModule* module2) {
  TypeDefinition type1 = module1->type(type_index1);
  TypeDefinition type2 = module2->type(type_index2);
  TypeDefinition::Kind kind1 = type1.kind;
  TypeDefinition::Kind kind2 = type2.kind;
  if (type1.is_shared != type2.is_shared) {
    return HeapType::kTop;
  }
  bool both_shared = type1.is_shared;
  {
    int depth1 = GetSubtypingDepth(module1, type_index1);
    int depth2 = GetSubtypingDepth(module2, type_index2);
    while (depth1 > depth2) {
      type_index1 = module1->supertype(type_index1);
      depth1--;
    }
    while (depth2 > depth1) {
      type_index2 = module2->supertype(type_index2);
      depth2--;
    }
  }
  DCHECK_NE(type_index1, kNoSuperType);
  DCHECK_NE(type_index2, kNoSuperType);
  while (type_index1 != kNoSuperType &&
         !(type_index1 == type_index2 && module1 == module2) &&
         !EquivalentIndices(type_index1, type_index2, module1, module2)) {
    type_index1 = module1->supertype(type_index1);
    type_index2 = module2->supertype(type_index2);
  }
  DCHECK_EQ(type_index1 == kNoSuperType, type_index2 == kNoSuperType);
  if (type_index1 != kNoSuperType) {
    return static_cast<HeapType::Representation>(type_index1.index);
  }
  switch (kind1) {
    case TypeDefinition::kFunction:
      switch (kind2) {
        case TypeDefinition::kFunction:
          return MaybeShared(HeapType::kFunc, both_shared);
        case TypeDefinition::kStruct:
        case TypeDefinition::kArray:
          return HeapType::kTop;
      }
    case TypeDefinition::kStruct:
      switch (kind2) {
        case TypeDefinition::kFunction:
          return HeapType::kTop;
        case TypeDefinition::kStruct:
          return MaybeShared(HeapType::kStruct, both_shared);
        case TypeDefinition::kArray:
          return MaybeShared(HeapType::kEq, both_shared);
      }
    case TypeDefinition::kArray:
      switch (kind2) {
        case TypeDefinition::kFunction:
          return HeapType::kTop;
        case TypeDefinition::kStruct:
          return MaybeShared(HeapType::kEq, both_shared);
        case TypeDefinition::kArray:
          return MaybeShared(HeapType::kArray, both_shared);
      }
  }
}

// Returns the least common ancestor of an abstract HeapType {heap1}, and
// another HeapType {heap2}.
HeapType::Representation CommonAncestorWithAbstract(HeapType heap1,
                                                    HeapType heap2,
                                                    const WasmModule* module2) {
  DCHECK(heap1.is_abstract());
  // Passing {module2} with {heap1} below is fine since {heap1} is abstract.
  bool is_shared = IsShared(heap1, module2);
  if (is_shared != IsShared(heap2, module2)) {
    return HeapType::kTop;
  }

  // TODO(mliedtke): These types should be normalized to the value type kTop and
  // kBottom and therefore should never appear here. Can we convert these into
  // assertions?
  if (heap1.is_top() || heap2.is_top()) return HeapType::kTop;
  if (heap1.is_bottom()) return heap2.representation();
  if (heap2.is_bottom()) return heap1.representation();

  HeapType::Representation repr_non_shared2 = heap2.representation_non_shared();
  switch (heap1.representation_non_shared()) {
    case HeapType::kFunc: {
      if (repr_non_shared2 == HeapType::kFunc ||
          repr_non_shared2 == HeapType::kNoFunc ||
          (heap2.is_index() && module2->has_signature(heap2.ref_index()))) {
        return MaybeShared(HeapType::kFunc, is_shared);
      } else {
        return HeapType::kTop;
      }
    }
    case HeapType::kAny: {
      switch (repr_non_shared2) {
        case HeapType::kI31:
        case HeapType::kNone:
        case HeapType::kEq:
        case HeapType::kStruct:
        case HeapType::kArray:
        case HeapType::kAny:
        case HeapType::kString:
          return MaybeShared(HeapType::kAny, is_shared);
        case HeapType::kFunc:
        case HeapType::kExtern:
        case HeapType::kExternString:
        case HeapType::kNoExtern:
        case HeapType::kNoFunc:
        case HeapType::kStringViewIter:
        case HeapType::kStringViewWtf8:
        case HeapType::kStringViewWtf16:
        case HeapType::kExn:
        case HeapType::kNoExn:
          return HeapType::kTop;
        default:
          return module2->has_signature(heap2.ref_index())
                     ? HeapType::kTop
                     : MaybeShared(HeapType::kAny, is_shared);
      }
    }
    case HeapType::kEq: {
      switch (repr_non_shared2) {
        case HeapType::kI31:
        case HeapType::kNone:
        case HeapType::kEq:
        case HeapType::kStruct:
        case HeapType::kArray:
          return MaybeShared(HeapType::kEq, is_shared);
        case HeapType::kAny:
        case HeapType::kString:
          return MaybeShared(HeapType::kAny, is_shared);
        case HeapType::kFunc:
        case HeapType::kExtern:
        case HeapType::kExternString:
        case HeapType::kNoExtern:
        case HeapType::kNoFunc:
        case HeapType::kStringViewIter:
        case HeapType::kStringViewWtf8:
        case HeapType::kStringViewWtf16:
        case HeapType::kExn:
        case HeapType::kNoExn:
          return HeapType::kTop;
        default:
          return module2->has_signature(heap2.ref_index())
                     ? HeapType::kTop
                     : MaybeShared(HeapType::kEq, is_shared);
      }
    }
    case HeapType::kI31:
      switch (repr_non_shared2) {
        case HeapType::kI31:
        case HeapType::kNone:
          return MaybeShared(HeapType::kI31, is_shared);
        case HeapType::kEq:
        case HeapType::kStruct:
        case HeapType::kArray:
          return MaybeShared(HeapType::kEq, is_shared);
        case HeapType::kAny:
        case HeapType::kString:
          return MaybeShared(HeapType::kAny, is_shared);
        case HeapType::kFunc:
        case HeapType::kExtern:
        case HeapType::kExternString:
        case HeapType::kNoExtern:
        case HeapType::kNoFunc:
        case HeapType::kStringViewIter:
        case HeapType::kStringViewWtf8:
        case HeapType::kStringViewWtf16:
        case HeapType::kExn:
        case HeapType::kNoExn:
          return HeapType::kTop;
        default:
          return module2->has_signature(heap2.ref_index())
                     ? HeapType::kTop
                     : MaybeShared(HeapType::kEq, is_shared);
      }
    case HeapType::kStruct:
      switch (repr_non_shared2) {
        case HeapType::kStruct:
        case HeapType::kNone:
          return MaybeShared(HeapType::kStruct, is_shared);
        case HeapType::kArray:
        case HeapType::kI31:
        case HeapType::kEq:
          return MaybeShared(HeapType::kEq, is_shared);
        case HeapType::kAny:
        case HeapType::kString:
          return MaybeShared(HeapType::kAny, is_shared);
        case HeapType::kFunc:
        case HeapType::kExtern:
        case HeapType::kExternString:
        case HeapType::kNoExtern:
        case HeapType::kNoFunc:
        case HeapType::kStringViewIter:
        case HeapType::kStringViewWtf8:
        case HeapType::kStringViewWtf16:
        case HeapType::kExn:
        case HeapType::kNoExn:
          return HeapType::kTop;
        default:
          return module2->has_struct(heap2.ref_index())
                     ? MaybeShared(HeapType::kStruct, is_shared)
                 : module2->has_array(heap2.ref_index())
                     ? MaybeShared(HeapType::kEq, is_shared)
                     : HeapType::kTop;
      }
    case HeapType::kArray:
      switch (repr_non_shared2) {
        case HeapType::kArray:
        case HeapType::kNone:
          return MaybeShared(HeapType::kArray, is_shared);
        case HeapType::kStruct:
        case HeapType::kI31:
        case HeapType::kEq:
          return MaybeShared(HeapType::kEq, is_shared);
        case HeapType::kAny:
        case HeapType::kString:
          return MaybeShared(HeapType::kAny, is_shared);
        case HeapType::kFunc:
        case HeapType::kExtern:
        case HeapType::kExternString:
        case HeapType::kNoExtern:
        case HeapType::kNoFunc:
        case HeapType::kStringViewIter:
        case HeapType::kStringViewWtf8:
        case HeapType::kStringViewWtf16:
        case HeapType::kExn:
        case HeapType::kNoExn:
          return HeapType::kTop;
        default:
          return module2->has_array(heap2.ref_index())
                     ? MaybeShared(HeapType::kArray, is_shared)
                 : module2->has_struct(heap2.ref_index())
                     ? MaybeShared(HeapType::kEq, is_shared)
                     : HeapType::kTop;
      }
    case HeapType::kNone:
      switch (repr_non_shared2) {
        case HeapType::kArray:
        case HeapType::kNone:
        case HeapType::kStruct:
        case HeapType::kI31:
        case HeapType::kEq:
        case HeapType::kAny:
        case HeapType::kString:
          return heap2.representation();
        case HeapType::kExtern:
        case HeapType::kExternString:
        case HeapType::kNoExtern:
        case HeapType::kNoFunc:
        case HeapType::kFunc:
        case HeapType::kStringViewIter:
        case HeapType::kStringViewWtf8:
        case HeapType::kStringViewWtf16:
        case HeapType::kExn:
        case HeapType::kNoExn:
          return HeapType::kTop;
        default:
          return module2->has_signature(heap2.ref_index())
                     ? HeapType::kTop
                     : heap2.representation();
      }
    case HeapType::kNoFunc:
      return (repr_non_shared2 == HeapType::kNoFunc ||
              repr_non_shared2 == HeapType::kFunc ||
              (heap2.is_index() && module2->has_signature(heap2.ref_index())))
                 ? heap2.representation()
                 : HeapType::kTop;
    case HeapType::kNoExtern:
      return repr_non_shared2 == HeapType::kExtern ||
                     repr_non_shared2 == HeapType::kNoExtern ||
                     repr_non_shared2 == HeapType::kExternString
                 ? heap2.representation()
                 : HeapType::kTop;
    case HeapType::kExtern:
      return repr_non_shared2 == HeapType::kExtern ||
                     repr_non_shared2 == HeapType::kNoExtern ||
                     repr_non_shared2 == HeapType::kExternString
                 ? MaybeShared(HeapType::kExtern, is_shared)
                 : HeapType::kTop;
    case HeapType::kExternString:
      return repr_non_shared2 == HeapType::kExtern
                 ? MaybeShared(HeapType::kExtern, is_shared)
             : (repr_non_shared2 == HeapType::kNoExtern ||
                repr_non_shared2 == HeapType::kExternString)
                 ? MaybeShared(HeapType::kExternString, is_shared)
                 : HeapType::kTop;
    case HeapType::kNoExn:
      return repr_non_shared2 == HeapType::kExn ||
                     repr_non_shared2 == HeapType::kNoExn
                 ? heap2.representation()
                 : HeapType::kTop;
    case HeapType::kExn:
      return repr_non_shared2 == HeapType::kExn ||
                     repr_non_shared2 == HeapType::kNoExn
                 ? MaybeShared(HeapType::kExn, is_shared)
                 : HeapType::kTop;
    case HeapType::kString: {
      switch (repr_non_shared2) {
        case HeapType::kI31:
        case HeapType::kEq:
        case HeapType::kStruct:
        case HeapType::kArray:
        case HeapType::kAny:
          return MaybeShared(HeapType::kAny, is_shared);
        case HeapType::kNone:
        case HeapType::kString:
          return MaybeShared(HeapType::kString, is_shared);
        case HeapType::kFunc:
        case HeapType::kExtern:
        case HeapType::kExternString:
        case HeapType::kNoExtern:
        case HeapType::kNoFunc:
        case HeapType::kStringViewIter:
        case HeapType::kStringViewWtf8:
        case HeapType::kStringViewWtf16:
        case HeapType::kExn:
        case HeapType::kNoExn:
          return HeapType::kTop;
        default:
          return module2->has_signature(heap2.ref_index())
                     ? HeapType::kTop
                     : MaybeShared(HeapType::kAny, is_shared);
      }
    }
    case HeapType::kStringViewIter:
    case HeapType::kStringViewWtf16:
    case HeapType::kStringViewWtf8:
      return heap1 == heap2 ? heap1.representation() : HeapType::kTop;
    default:
      UNREACHABLE();
  }
}
}  // namespace

V8_EXPORT_PRIVATE TypeInModule Union(ValueType type1, ValueType type2,
                                     const WasmModule* module1,
                                     const WasmModule* module2) {
  if (type1 == kWasmTop || type2 == kWasmTop) return {kWasmTop, module1};
  if (type1 == kWasmBottom) return {type2, module2};
  if (type2 == kWasmBottom) return {type1, module1};
  if (!type1.is_object_reference() || !type2.is_object_reference()) {
    return {EquivalentTypes(type1, type2, module1, module2) ? type1 : kWasmTop,
            module1};
  }
  Nullability nullability =
      type1.is_nullable() || type2.is_nullable() ? kNullable : kNonNullable;
  HeapType heap1 = type1.heap_type();
  HeapType heap2 = type2.heap_type();
  if (heap1 == heap2 && module1 == module2) {
    return {ValueType::RefMaybeNull(heap1, nullability), module1};
  }
  HeapType::Representation result_repr;
  const WasmModule* result_module;
  if (heap1.is_abstract()) {
    result_repr = CommonAncestorWithAbstract(heap1, heap2, module2);
    result_module = module2;
  } else if (heap2.is_abstract()) {
    result_repr = CommonAncestorWithAbstract(heap2, heap1, module1);
    result_module = module1;
  } else {
    result_repr =
        CommonAncestor(heap1.ref_index(), heap2.ref_index(), module1, module2);
    result_module = module1;
  }
  // The type could only be kBottom if the input was kBottom but any kBottom
  // HeapType should be "normalized" to kWasmBottom ValueType.
  DCHECK_NE(result_repr, HeapType::kBottom);
  return {result_repr == HeapType::kTop
              ? kWasmTop
              : ValueType::RefMaybeNull(result_repr, nullability),
          result_module};
}

TypeInModule Intersection(ValueType type1, ValueType type2,
                          const WasmModule* module1,
                          const WasmModule* module2) {
  if (type1 == kWasmTop) return {type2, module2};
  if (type2 == kWasmTop) return {type1, module1};
  if (!type1.is_object_reference() || !type2.is_object_reference()) {
    return {
        EquivalentTypes(type1, type2, module1, module2) ? type1 : kWasmBottom,
        module1};
  }
  Nullability nullability =
      type1.is_nullable() && type2.is_nullable() ? kNullable : kNonNullable;
  // non-nullable null type is not a valid type.
  if (nullability == kNonNullable && (IsNullSentinel(type1.heap_type()) ||
                                      IsNullSentinel(type2.heap_type()))) {
    return {kWasmBottom, module1};
  }
  if (IsHeapSubtypeOf(type1.heap_type(), type2.heap_type(), module1, module2)) {
    return TypeInModule{ValueType::RefMaybeNull(type1.heap_type(), nullability),
                        module1};
  }
  if (IsHeapSubtypeOf(type2.heap_type(), type1.heap_type(), module2, module1)) {
    return TypeInModule{ValueType::RefMaybeNull(type2.heap_type(), nullability),
                        module2};
  }
  if (nullability == kNonNullable) {
    return {kWasmBottom, module1};
  }
  // Check for common null representation.
  ValueType null_type1 = ToNullSentinel({type1, module1});
  if (null_type1 == ToNullSentinel({type2, module2})) {
    return {null_type1, module1};
  }
  return {kWasmBottom, module1};
}

ValueType ToNullSentinel(TypeInModule type) {
  HeapType::Representation null_heap =
      NullSentinelImpl(type.type.heap_type(), type.module);
  DCHECK(
      IsHeapSubtypeOf(HeapType(null_heap), type.type.heap_type(), type.module));
  return ValueType::RefNull(null_heap);
}

bool IsSameTypeHierarchy(HeapType type1, HeapType type2,
                         const WasmModule* module) {
  return NullSentinelImpl(type1, module) == NullSentinelImpl(type2, module);
}

}  // namespace v8::internal::wasm
