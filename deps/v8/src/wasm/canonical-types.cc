// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/canonical-types.h"

#include "src/base/hashing.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/heap/heap-inl.h"
#include "src/init/v8.h"
#include "src/roots/roots-inl.h"
#include "src/utils/utils.h"
#include "src/wasm/names-provider.h"
#include "src/wasm/std-object-sizes.h"
#include "src/wasm/wasm-engine.h"

namespace v8::internal::wasm {

TypeCanonicalizer* GetTypeCanonicalizer() {
  return GetWasmEngine()->type_canonicalizer();
}

TypeCanonicalizer::TypeCanonicalizer() { AddPredefinedArrayTypes(); }

void TypeCanonicalizer::CheckMaxCanonicalIndex() const {
  DCHECK_EQ(canonical_types_.size(), canonical_supertypes_.size());
  if (V8_UNLIKELY(canonical_types_.size() > kMaxCanonicalTypes)) {
    V8::FatalProcessOutOfMemory(nullptr, "too many canonicalized types");
  }
}

void TypeCanonicalizer::AddRecursiveGroup(WasmModule* module, uint32_t size) {
  if (size == 0) return;
  // If the caller knows statically that {size == 1}, it should have called
  // {AddRecursiveSingletonGroup} directly. For cases where this is not
  // statically determined we add this dispatch here.
  if (size == 1) return AddRecursiveSingletonGroup(module);

  uint32_t start_index = static_cast<uint32_t>(module->types.size() - size);

  // Multiple threads could try to register recursive groups concurrently.
  // TODO(manoskouk): Investigate if we can fine-grain the synchronization.
  base::SpinningMutexGuard mutex_guard(&mutex_);
  // Compute the first canonical index in the recgroup in the case that it does
  // not already exist.
  CanonicalTypeIndex first_new_canonical_index{
      static_cast<uint32_t>(canonical_types_.size())};

  // Create a snapshot of the zone; this will be restored in case we find a
  // matching recursion group.
  ZoneSnapshot zone_snapshot = zone_.Snapshot();

  DCHECK_GE(module->types.size(), start_index + size);
  CanonicalGroup group{&zone_, size, first_new_canonical_index};
  for (uint32_t i = 0; i < size; i++) {
    group.types[i] = CanonicalizeTypeDef(
        module, ModuleTypeIndex{start_index + i}, ModuleTypeIndex{start_index},
        first_new_canonical_index);
  }
  if (CanonicalTypeIndex canonical_index = FindCanonicalGroup(group);
      canonical_index.valid()) {
    // Delete zone memory from {CanonicalizeTypeDef} and {CanonicalGroup}.
    zone_snapshot.Restore(&zone_);

    // Identical group found. Map new types to the old types's canonical
    // representatives.
    for (uint32_t i = 0; i < size; i++) {
      CanonicalTypeIndex existing_type_index =
          CanonicalTypeIndex{canonical_index.index + i};
      module->isorecursive_canonical_type_ids[start_index + i] =
          existing_type_index;
    }
    return;
  }
  canonical_types_.resize(first_new_canonical_index.index + size);
  canonical_supertypes_.resize(first_new_canonical_index.index + size);
  CheckMaxCanonicalIndex();
  for (uint32_t i = 0; i < size; i++) {
    CanonicalType& canonical_type = group.types[i];
    uint32_t canonical_index = first_new_canonical_index.index + i;
    // {CanonicalGroup} allocates types in the Zone.
    DCHECK(zone_.Contains(&canonical_type));
    canonical_types_[canonical_index] = &canonical_type;
    canonical_supertypes_[canonical_index] = canonical_type.supertype;
    CanonicalTypeIndex canonical_id{canonical_index};
    module->isorecursive_canonical_type_ids[start_index + i] = canonical_id;
  }
  // Check that this canonical ID is not used yet.
  DCHECK(std::none_of(
      canonical_singleton_groups_.begin(), canonical_singleton_groups_.end(),
      [=](auto& entry) { return entry.index == first_new_canonical_index; }));
  DCHECK(std::none_of(
      canonical_groups_.begin(), canonical_groups_.end(),
      [=](auto& entry) { return entry.first == first_new_canonical_index; }));
  canonical_groups_.emplace(group);
}

void TypeCanonicalizer::AddRecursiveSingletonGroup(WasmModule* module) {
  DCHECK(!module->types.empty());
  uint32_t type_index = static_cast<uint32_t>(module->types.size() - 1);
  base::SpinningMutexGuard guard(&mutex_);
  CanonicalTypeIndex new_canonical_index{
      static_cast<uint32_t>(canonical_types_.size())};
  // Snapshot the zone before allocating the new type; the zone will be reset if
  // we find an identical type.
  ZoneSnapshot zone_snapshot = zone_.Snapshot();
  CanonicalType type =
      CanonicalizeTypeDef(module, ModuleTypeIndex{type_index},
                          ModuleTypeIndex{type_index}, new_canonical_index);
  CanonicalSingletonGroup group{type, new_canonical_index};
  if (CanonicalTypeIndex index = FindCanonicalGroup(group); index.valid()) {
    zone_snapshot.Restore(&zone_);
    module->isorecursive_canonical_type_ids[type_index] = index;
    return;
  }
  // Check that the new canonical ID is not used yet.
  DCHECK(std::none_of(
      canonical_singleton_groups_.begin(), canonical_singleton_groups_.end(),
      [=](auto& entry) { return entry.index == new_canonical_index; }));
  DCHECK(std::none_of(
      canonical_groups_.begin(), canonical_groups_.end(),
      [=](auto& entry) { return entry.first == new_canonical_index; }));
  // {group.type} is stack-allocated, whereas {canonical_singleton_groups_}
  // creates a long-lived zone-allocated copy of it.
  auto stored_group = canonical_singleton_groups_.emplace(group).first;
  canonical_types_.push_back(&stored_group->type);
  canonical_supertypes_.push_back(type.supertype);
  CheckMaxCanonicalIndex();
  module->isorecursive_canonical_type_ids[type_index] = new_canonical_index;
}

CanonicalTypeIndex TypeCanonicalizer::AddRecursiveGroup(
    const FunctionSig* sig) {
// Types in the signature must be module-independent.
#if DEBUG
  for (ValueType type : sig->all()) DCHECK(!type.has_index());
#endif
  const bool kFinal = true;
  const bool kNotShared = false;
  // Because of the checks above, we can treat the type_def as canonical.
  // TODO(366180605): It would be nice to not have to rely on a cast here.
  // Is there a way to avoid it? In the meantime, these asserts provide at
  // least partial assurances that the cast is safe:
  static_assert(sizeof(CanonicalValueType) == sizeof(ValueType));
  static_assert(CanonicalValueType::Primitive(kI32).raw_bit_field() ==
                ValueType::Primitive(kI32).raw_bit_field());
  CanonicalType canonical{reinterpret_cast<const CanonicalSig*>(sig),
                          CanonicalTypeIndex{kNoSuperType}, kFinal, kNotShared};
  base::SpinningMutexGuard guard(&mutex_);
  // Fast path lookup before canonicalizing (== copying into the
  // TypeCanonicalizer's zone) the function signature.
  CanonicalTypeIndex new_canonical_index{
      static_cast<uint32_t>(canonical_types_.size())};
  CanonicalTypeIndex index = FindCanonicalGroup(
      CanonicalSingletonGroup{canonical, new_canonical_index});
  if (index.valid()) return index;

  // Copy into this class's zone to store this as a new canonical function type.
  CanonicalSig::Builder builder(&zone_, sig->return_count(),
                                sig->parameter_count());
  for (ValueType ret : sig->returns()) {
    builder.AddReturn(CanonicalValueType{ret});
  }
  for (ValueType param : sig->parameters()) {
    builder.AddParam(CanonicalValueType{param});
  }
  canonical.function_sig = builder.Get();

  CanonicalSingletonGroup group{canonical, new_canonical_index};
  // Copying the signature shouldn't make a difference: There is no match.
  DCHECK(!FindCanonicalGroup(group).valid());
  // Check that the new canonical ID is not used yet.
  DCHECK(std::none_of(
      canonical_singleton_groups_.begin(), canonical_singleton_groups_.end(),
      [=](auto& entry) { return entry.index == new_canonical_index; }));
  DCHECK(std::none_of(
      canonical_groups_.begin(), canonical_groups_.end(),
      [=](auto& entry) { return entry.first == new_canonical_index; }));
  // {group.type} is stack-allocated, whereas {canonical_singleton_groups_}
  // creates a long-lived zone-allocated copy of it.
  const CanonicalSingletonGroup& stored_group =
      *canonical_singleton_groups_.emplace(group).first;
  canonical_types_.push_back(&stored_group.type);
  canonical_supertypes_.push_back(CanonicalTypeIndex{kNoSuperType});
  CheckMaxCanonicalIndex();
  return new_canonical_index;
}

const CanonicalSig* TypeCanonicalizer::LookupFunctionSignature(
    CanonicalTypeIndex index) const {
  base::SpinningMutexGuard mutex_guard(&mutex_);
  SBXCHECK(index.index < canonical_types_.size());
  const CanonicalType* type = get(index);
  DCHECK_EQ(type->kind, CanonicalType::kFunction);
  return type->function_sig;
}

const CanonicalStructType* TypeCanonicalizer::LookupStruct(
    CanonicalTypeIndex index) const {
  base::SpinningMutexGuard mutex_guard(&mutex_);
  SBXCHECK(index.index < canonical_types_.size());
  const CanonicalType* type = get(index);
  DCHECK_EQ(type->kind, CanonicalType::kStruct);
  return type->struct_type;
}

const CanonicalArrayType* TypeCanonicalizer::LookupArray(
    CanonicalTypeIndex index) const {
  base::SpinningMutexGuard mutex_guard(&mutex_);
  SBXCHECK(index.index < canonical_types_.size());
  const CanonicalType* type = get(index);
  DCHECK_EQ(type->kind, CanonicalType::kArray);
  return type->array_type;
}

void TypeCanonicalizer::AddPredefinedArrayTypes() {
  static constexpr std::pair<CanonicalTypeIndex, CanonicalValueType>
      kPredefinedArrayTypes[] = {{kPredefinedArrayI8Index, {kWasmI8}},
                                 {kPredefinedArrayI16Index, {kWasmI16}}};
  for (auto [index, element_type] : kPredefinedArrayTypes) {
    DCHECK_EQ(index.index, canonical_singleton_groups_.size());
    static constexpr bool kMutable = true;
    static constexpr bool kFinal = true;
    static constexpr bool kShared = false;  // TODO(14616): Fix this.
    CanonicalArrayType* type =
        zone_.New<CanonicalArrayType>(element_type, kMutable);
    CanonicalSingletonGroup group{
        .type = CanonicalType(type, CanonicalTypeIndex{kNoSuperType}, kFinal,
                              kShared),
        .index = index};
    const CanonicalSingletonGroup& stored_group =
        *canonical_singleton_groups_.emplace(group).first;
    canonical_types_.push_back(&stored_group.type);
    canonical_supertypes_.emplace_back(CanonicalTypeIndex{kNoSuperType});
    DCHECK_LE(canonical_types_.size(), kMaxCanonicalTypes);
  }
}

bool TypeCanonicalizer::IsCanonicalSubtype(CanonicalTypeIndex sub_index,
                                           CanonicalTypeIndex super_index) {
  // Fast path without synchronization:
  if (sub_index == super_index) return true;

  // Multiple threads could try to register and access recursive groups
  // concurrently.
  // TODO(manoskouk): Investigate if we can improve this synchronization.
  base::SpinningMutexGuard mutex_guard(&mutex_);
  return IsCanonicalSubtype_Locked(sub_index, super_index);
}
bool TypeCanonicalizer::IsCanonicalSubtype_Locked(
    CanonicalTypeIndex sub_index, CanonicalTypeIndex super_index) const {
  while (sub_index.valid()) {
    if (sub_index == super_index) return true;
    // TODO(jkummerow): Investigate if replacing this with
    // `sub_index = get(sub_index).supertype;`
    // has acceptable performance, which would allow us to save the memory
    // cost of storing {canonical_supertypes_}.
    sub_index = canonical_supertypes_[sub_index.index];
  }
  return false;
}

bool TypeCanonicalizer::IsCanonicalSubtype(ModuleTypeIndex sub_index,
                                           ModuleTypeIndex super_index,
                                           const WasmModule* sub_module,
                                           const WasmModule* super_module) {
  CanonicalTypeIndex canonical_super =
      super_module->canonical_type_id(super_index);
  CanonicalTypeIndex canonical_sub = sub_module->canonical_type_id(sub_index);
  return IsCanonicalSubtype(canonical_sub, canonical_super);
}

bool TypeCanonicalizer::IsShared(CanonicalValueType type) const {
  return HeapType(type.heap_representation()).is_abstract_shared() ||
         (type.has_index() && get(type)->is_shared);
}

bool TypeCanonicalizer::IsHeapSubtype(CanonicalValueType sub,
                                      CanonicalValueType super) const {
  DCHECK(sub.is_object_reference() && super.is_object_reference());

  base::SpinningMutexGuard mutex_guard(&mutex_);
  if (IsShared(sub) != IsShared(super)) return false;

  HeapType::Representation sub_repr_non_shared =
      sub.heap_representation_non_shared();
  HeapType::Representation super_repr_non_shared =
      super.heap_representation_non_shared();
  switch (sub_repr_non_shared) {
    case HeapType::kFunc:
    case HeapType::kAny:
    case HeapType::kExtern:
    case HeapType::kExn:
    case HeapType::kStringViewWtf8:
    case HeapType::kStringViewWtf16:
    case HeapType::kStringViewIter:
      return sub_repr_non_shared == super_repr_non_shared;
    case HeapType::kEq:
    case HeapType::kString:
      return sub_repr_non_shared == super_repr_non_shared ||
             super_repr_non_shared == HeapType::kAny;
    case HeapType::kExternString:
      return super_repr_non_shared == sub_repr_non_shared ||
             super_repr_non_shared == HeapType::kExtern;
    case HeapType::kI31:
    case HeapType::kStruct:
    case HeapType::kArray:
      return super_repr_non_shared == sub_repr_non_shared ||
             super_repr_non_shared == HeapType::kEq ||
             super_repr_non_shared == HeapType::kAny;
    case HeapType::kBottom:
    case HeapType::kTop:
      UNREACHABLE();
    case HeapType::kNone:
      // none is a subtype of every non-func, non-extern and non-exn reference
      // type under wasm-gc.
      if (super.has_index()) {
        return get(super)->kind != CanonicalType::kFunction;
      }
      return super_repr_non_shared == HeapType::kAny ||
             super_repr_non_shared == HeapType::kEq ||
             super_repr_non_shared == HeapType::kI31 ||
             super_repr_non_shared == HeapType::kArray ||
             super_repr_non_shared == HeapType::kStruct ||
             super_repr_non_shared == HeapType::kString ||
             super_repr_non_shared == HeapType::kStringViewWtf16 ||
             super_repr_non_shared == HeapType::kStringViewWtf8 ||
             super_repr_non_shared == HeapType::kStringViewIter ||
             super_repr_non_shared == HeapType::kNone;
    case HeapType::kNoExtern:
      return super_repr_non_shared == HeapType::kNoExtern ||
             super_repr_non_shared == HeapType::kExtern ||
             super_repr_non_shared == HeapType::kExternString;
    case HeapType::kNoExn:
      return super_repr_non_shared == HeapType::kExn ||
             super_repr_non_shared == HeapType::kNoExn;
    case HeapType::kNoFunc:
      // nofunc is a subtype of every funcref type under wasm-gc.
      if (super.has_index()) {
        return get(super)->kind == CanonicalType::kFunction;
      }
      return super_repr_non_shared == HeapType::kNoFunc ||
             super_repr_non_shared == HeapType::kFunc;
    default:
      break;
  }

  DCHECK(sub.has_index());
  CanonicalTypeIndex sub_index = sub.ref_index();

  switch (super_repr_non_shared) {
    case HeapType::kFunc:
      return get(sub_index)->kind == CanonicalType::kFunction;
    case HeapType::kStruct:
      return get(sub_index)->kind == CanonicalType::kStruct;
    case HeapType::kEq:
    case HeapType::kAny:
      return get(sub_index)->kind != CanonicalType::kFunction;
    case HeapType::kArray:
      return get(sub_index)->kind == CanonicalType::kArray;
    case HeapType::kI31:
    case HeapType::kExtern:
    case HeapType::kExternString:
    case HeapType::kExn:
    case HeapType::kString:
    case HeapType::kStringViewWtf8:
    case HeapType::kStringViewWtf16:
    case HeapType::kStringViewIter:
    case HeapType::kNone:
    case HeapType::kNoExtern:
    case HeapType::kNoFunc:
    case HeapType::kNoExn:
      return false;
    case HeapType::kBottom:
    case HeapType::kTop:
      UNREACHABLE();
    default:
      break;
  }

  DCHECK(super.has_index());
  CanonicalTypeIndex super_index = super.ref_index();
  // The {IsSubtypeOf} entry point already has a fast path checking full type
  // equality; here we catch (ref $x) being a subtype of (ref null $x).
  if (sub_index == super_index) return true;
  return IsCanonicalSubtype_Locked(sub_index, super_index);
}

void TypeCanonicalizer::EmptyStorageForTesting() {
  // Any remaining native modules might reference the types we're about to
  // clear.
  CHECK_EQ(GetWasmEngine()->NativeModuleCount(), 0);

  base::SpinningMutexGuard mutex_guard(&mutex_);
  canonical_types_.clear();
  canonical_supertypes_.clear();
  canonical_groups_.clear();
  canonical_singleton_groups_.clear();
  zone_.Reset();
  AddPredefinedArrayTypes();
}

TypeCanonicalizer::CanonicalType TypeCanonicalizer::CanonicalizeTypeDef(
    const WasmModule* module, ModuleTypeIndex module_type_idx,
    ModuleTypeIndex recgroup_start,
    CanonicalTypeIndex canonical_recgroup_start) {
  mutex_.AssertHeld();  // The caller must hold the mutex.

  auto CanonicalizeTypeIndex = [=](ModuleTypeIndex type_index) {
    DCHECK(type_index.valid());
    return type_index < recgroup_start
               // This references a type from an earlier recgroup; use the
               // already-canonicalized type index.
               ? module->canonical_type_id(type_index)
               // For types within the same recgroup, generate indexes assuming
               // that this is a new canonical recgroup.
               : CanonicalTypeIndex{canonical_recgroup_start.index +
                                    (type_index.index - recgroup_start.index)};
  };

  auto CanonicalizeValueType = [=](ValueType type) {
    if (!type.has_index()) return CanonicalValueType{type};
    static_assert(kMaxCanonicalTypes <= (1u << ValueType::kHeapTypeBits));
    return CanonicalValueType::FromIndex(
        type.kind(), CanonicalizeTypeIndex(type.ref_index()));
  };

  TypeDefinition type = module->type(module_type_idx);
  CanonicalTypeIndex supertype = type.supertype.valid()
                                     ? CanonicalizeTypeIndex(type.supertype)
                                     : CanonicalTypeIndex::Invalid();
  switch (type.kind) {
    case TypeDefinition::kFunction: {
      const FunctionSig* original_sig = type.function_sig;
      CanonicalSig::Builder builder(&zone_, original_sig->return_count(),
                                    original_sig->parameter_count());
      for (ValueType ret : original_sig->returns()) {
        builder.AddReturn(CanonicalizeValueType(ret));
      }
      for (ValueType param : original_sig->parameters()) {
        builder.AddParam(CanonicalizeValueType(param));
      }
      return CanonicalType(builder.Get(), supertype, type.is_final,
                           type.is_shared);
    }
    case TypeDefinition::kStruct: {
      const StructType* original_type = type.struct_type;
      CanonicalStructType::Builder builder(&zone_,
                                           original_type->field_count());
      for (uint32_t i = 0; i < original_type->field_count(); i++) {
        builder.AddField(CanonicalizeValueType(original_type->field(i)),
                         original_type->mutability(i),
                         original_type->field_offset(i));
      }
      builder.set_total_fields_size(original_type->total_fields_size());
      return CanonicalType(
          builder.Build(CanonicalStructType::Builder::kUseProvidedOffsets),
          supertype, type.is_final, type.is_shared);
    }
    case TypeDefinition::kArray: {
      CanonicalValueType element_type =
          CanonicalizeValueType(type.array_type->element_type());
      CanonicalArrayType* array_type = zone_.New<CanonicalArrayType>(
          element_type, type.array_type->mutability());
      return CanonicalType(array_type, supertype, type.is_final,
                           type.is_shared);
    }
  }
}

// Returns the index of the canonical representative of the first type in this
// group if it exists, and `CanonicalTypeIndex::Invalid()` otherwise.
CanonicalTypeIndex TypeCanonicalizer::FindCanonicalGroup(
    const CanonicalGroup& group) const {
  // Groups of size 0 do not make sense here; groups of size 1 should use
  // {CanonicalSingletonGroup} (see below).
  DCHECK_LT(1, group.types.size());
  auto it = canonical_groups_.find(group);
  return it == canonical_groups_.end() ? CanonicalTypeIndex::Invalid()
                                       : it->first;
}

// Returns the canonical index of the given group if it already exists.
CanonicalTypeIndex TypeCanonicalizer::FindCanonicalGroup(
    const CanonicalSingletonGroup& group) const {
  auto it = canonical_singleton_groups_.find(group);
  static_assert(kMaxCanonicalTypes <= kMaxInt);
  return it == canonical_singleton_groups_.end() ? CanonicalTypeIndex::Invalid()
                                                 : it->index;
}

size_t TypeCanonicalizer::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(TypeCanonicalizer, 240);
  // The storage of the canonical group's types is accounted for via the
  // allocator below (which tracks the zone memory).
  base::SpinningMutexGuard mutex_guard(&mutex_);
  size_t result = ContentSize(canonical_supertypes_);
  result += ContentSize(canonical_groups_);
  result += ContentSize(canonical_singleton_groups_);
  result += ContentSize(canonical_types_);
  result += allocator_.GetCurrentMemoryUsage();
  if (v8_flags.trace_wasm_offheap_memory) {
    PrintF("TypeCanonicalizer: %zu\n", result);
  }
  return result;
}

size_t TypeCanonicalizer::GetCurrentNumberOfTypes() const {
  base::SpinningMutexGuard mutex_guard(&mutex_);
  return canonical_types_.size();
}

// static
void TypeCanonicalizer::PrepareForCanonicalTypeId(Isolate* isolate,
                                                  CanonicalTypeIndex id) {
  if (!id.valid()) return;
  Heap* heap = isolate->heap();
  // {2 * (id + 1)} needs to fit in an int.
  CHECK_LE(id.index, kMaxInt / 2 - 1);
  // Canonical types and wrappers are zero-indexed.
  const int length = id.index + 1;
  // The fast path is non-handlified.
  Tagged<WeakFixedArray> old_rtts_raw = heap->wasm_canonical_rtts();
  Tagged<WeakFixedArray> old_wrappers_raw = heap->js_to_wasm_wrappers();

  // Fast path: Lengths are sufficient.
  int old_length = old_rtts_raw->length();
  DCHECK_EQ(old_length, old_wrappers_raw->length());
  if (old_length >= length) return;

  // Allocate bigger WeakFixedArrays for rtts and wrappers. Grow them
  // exponentially.
  const int new_length = std::max(old_length * 3 / 2, length);
  CHECK_LT(old_length, new_length);

  // Allocation can invalidate previous unhandled pointers.
  DirectHandle<WeakFixedArray> old_rtts{old_rtts_raw, isolate};
  DirectHandle<WeakFixedArray> old_wrappers{old_wrappers_raw, isolate};
  old_rtts_raw = old_wrappers_raw = {};

  // We allocate the WeakFixedArray filled with undefined values, as we cannot
  // pass the cleared value in a Handle (see https://crbug.com/364591622). We
  // overwrite the new entries via {MemsetTagged} afterwards.
  DirectHandle<WeakFixedArray> new_rtts =
      WeakFixedArray::New(isolate, new_length, AllocationType::kOld);
  WeakFixedArray::CopyElements(isolate, *new_rtts, 0, *old_rtts, 0, old_length);
  MemsetTagged(new_rtts->RawFieldOfFirstElement() + old_length,
               ClearedValue(isolate), new_length - old_length);
  DirectHandle<WeakFixedArray> new_wrappers =
      WeakFixedArray::New(isolate, new_length, AllocationType::kOld);
  WeakFixedArray::CopyElements(isolate, *new_wrappers, 0, *old_wrappers, 0,
                               old_length);
  MemsetTagged(new_wrappers->RawFieldOfFirstElement() + old_length,
               ClearedValue(isolate), new_length - old_length);
  heap->SetWasmCanonicalRttsAndJSToWasmWrappers(*new_rtts, *new_wrappers);
}

// static
void TypeCanonicalizer::ClearWasmCanonicalTypesForTesting(Isolate* isolate) {
  ReadOnlyRoots roots(isolate);
  isolate->heap()->SetWasmCanonicalRttsAndJSToWasmWrappers(
      roots.empty_weak_fixed_array(), roots.empty_weak_fixed_array());
}

bool TypeCanonicalizer::IsFunctionSignature(CanonicalTypeIndex index) const {
  base::SpinningMutexGuard mutex_guard(&mutex_);
  SBXCHECK(index.index < canonical_types_.size());
  return get(index)->kind == CanonicalType::kFunction;
}
bool TypeCanonicalizer::IsStruct(CanonicalTypeIndex index) const {
  base::SpinningMutexGuard mutex_guard(&mutex_);
  SBXCHECK(index.index < canonical_types_.size());
  return get(index)->kind == CanonicalType::kStruct;
}
bool TypeCanonicalizer::IsArray(CanonicalTypeIndex index) const {
  base::SpinningMutexGuard mutex_guard(&mutex_);
  SBXCHECK(index.index < canonical_types_.size());
  return get(index)->kind == CanonicalType::kArray;
}

CanonicalTypeIndex TypeCanonicalizer::FindIndex_Slow(
    const CanonicalSig* sig) const {
  // TODO(jkummerow): Make this faster. The plan is to allocate an extra
  // slot in the Zone immediately preceding each CanonicalSig, so we can
  // get from the sig's address to that slot's address via pointer arithmetic.
  // For now, just search through all known signatures, which is acceptable
  // as long as only the type-reflection proposal needs this.
  // TODO(42210967): Improve this before shipping Type Reflection.
  for (uint32_t i = 0; i < canonical_types_.size(); i++) {
    const CanonicalType* type = canonical_types_[i];
    if (type->kind == CanonicalType::kFunction && type->function_sig == sig) {
      return CanonicalTypeIndex{i};
    }
  }
  // If callers have a CanonicalSig* to pass into this function, the
  // type canonicalizer must know about this sig.
  UNREACHABLE();
}

#ifdef DEBUG
bool TypeCanonicalizer::Contains(const CanonicalSig* sig) const {
  base::SpinningMutexGuard mutex_guard(&mutex_);
  return zone_.Contains(sig);
}
#endif

}  // namespace v8::internal::wasm
