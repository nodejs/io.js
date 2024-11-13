// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/canonical-types.h"

#include "src/init/v8.h"
#include "src/utils/utils.h"
#include "src/wasm/std-object-sizes.h"
#include "src/wasm/wasm-engine.h"

namespace v8::internal::wasm {

TypeCanonicalizer* GetTypeCanonicalizer() {
  return GetWasmEngine()->type_canonicalizer();
}

TypeCanonicalizer::TypeCanonicalizer() { AddPredefinedArrayTypes(); }

// Inside the TypeCanonicalizer, we use ValueType instances constructed
// from canonical type indices, so we can't let them get bigger than what
// we have storage space for. Code outside the TypeCanonicalizer already
// supports up to Smi range for canonical type indices.
// TODO(jkummerow): Raise this limit. Possible options:
// - increase the size of ValueType::HeapTypeField, using currently-unused bits.
// - change the encoding of ValueType: one bit says whether it's a ref type,
//   the other bits then encode the index or the kind of non-ref type.
// - refactor the TypeCanonicalizer's internals to no longer use ValueTypes
//   and related infrastructure, and use a wider encoding of canonicalized
//   type indices only here.
// - wait for 32-bit platforms to no longer be relevant, and increase the
//   size of ValueType to 64 bits.
// None of this seems urgent, as we have no evidence of the current limit
// being an actual limitation in practice.
static constexpr size_t kMaxCanonicalTypes = kV8MaxWasmTypes;
// We don't want any valid modules to fail canonicalization.
static_assert(kMaxCanonicalTypes >= kV8MaxWasmTypes);
// We want the invalid index to fail any range checks.
static_assert(kInvalidCanonicalIndex > kMaxCanonicalTypes);

void TypeCanonicalizer::CheckMaxCanonicalIndex() const {
  if (canonical_supertypes_.size() > kMaxCanonicalTypes) {
    V8::FatalProcessOutOfMemory(nullptr, "too many canonicalized types");
  }
}

void TypeCanonicalizer::AddRecursiveGroup(WasmModule* module, uint32_t size) {
  AddRecursiveGroup(module, size,
                    static_cast<uint32_t>(module->types.size() - size));
}

void TypeCanonicalizer::AddRecursiveGroup(WasmModule* module, uint32_t size,
                                          uint32_t start_index) {
  if (size == 0) return;
  // If the caller knows statically that {size == 1}, it should have called
  // {AddRecursiveSingletonGroup} directly. For cases where this is not
  // statically determined we add this dispatch here.
  if (size == 1) return AddRecursiveSingletonGroup(module, start_index);

  // Multiple threads could try to register recursive groups concurrently.
  // TODO(manoskouk): Investigate if we can fine-grain the synchronization.
  base::MutexGuard mutex_guard(&mutex_);
  DCHECK_GE(module->types.size(), start_index + size);
  CanonicalGroup group{&zone_, size};
  for (uint32_t i = 0; i < size; i++) {
    group.types[i] = CanonicalizeTypeDef(module, module->types[start_index + i],
                                         start_index);
  }
  int canonical_index = FindCanonicalGroup(group);
  if (canonical_index >= 0) {
    // Identical group found. Map new types to the old types's canonical
    // representatives.
    for (uint32_t i = 0; i < size; i++) {
      module->isorecursive_canonical_type_ids[start_index + i] =
          canonical_index + i;
    }
    // TODO(clemensb): Avoid leaking the zone storage allocated for {group}
    // (both for the {Vector} in {CanonicalGroup}, but also the storage
    // allocated in {CanonicalizeTypeDef{).
    return;
  }
  // Identical group not found. Add new canonical representatives for the new
  // types.
  uint32_t first_canonical_index =
      static_cast<uint32_t>(canonical_supertypes_.size());
  canonical_supertypes_.resize(first_canonical_index + size);
  CheckMaxCanonicalIndex();
  for (uint32_t i = 0; i < size; i++) {
    CanonicalType& canonical_type = group.types[i];
    // Compute the canonical index of the supertype: If it is relative, we
    // need to add {first_canonical_index}.
    canonical_supertypes_[first_canonical_index + i] =
        canonical_type.is_relative_supertype
            ? canonical_type.type_def.supertype + first_canonical_index
            : canonical_type.type_def.supertype;
    module->isorecursive_canonical_type_ids[start_index + i] =
        first_canonical_index + i;
  }
  // Check that this canonical ID is not used yet.
  DCHECK(std::none_of(
      canonical_singleton_groups_.begin(), canonical_singleton_groups_.end(),
      [=](auto& entry) { return entry.second == first_canonical_index; }));
  DCHECK(std::none_of(
      canonical_groups_.begin(), canonical_groups_.end(),
      [=](auto& entry) { return entry.second == first_canonical_index; }));
  canonical_groups_.emplace(group, first_canonical_index);
}

void TypeCanonicalizer::AddRecursiveSingletonGroup(WasmModule* module) {
  uint32_t start_index = static_cast<uint32_t>(module->types.size() - 1);
  return AddRecursiveSingletonGroup(module, start_index);
}

void TypeCanonicalizer::AddRecursiveSingletonGroup(WasmModule* module,
                                                   uint32_t start_index) {
  DCHECK_GT(module->types.size(), start_index);
  // Multiple threads could try to register recursive groups concurrently.
  base::MutexGuard mutex_guard(&mutex_);
  CanonicalSingletonGroup group{
      CanonicalizeTypeDef(module, module->types[start_index], start_index)};
  int canonical_index = FindCanonicalGroup(group);
  if (canonical_index >= 0) {
    // Identical group found. Map new types to the old types's canonical
    // representatives.
    module->isorecursive_canonical_type_ids[start_index] = canonical_index;
    // TODO(clemensb): See above: Avoid leaking zone memory.
    return;
  }
  // Identical group not found. Add new canonical representatives for the new
  // types.
  uint32_t first_canonical_index =
      static_cast<uint32_t>(canonical_supertypes_.size());
  canonical_supertypes_.resize(first_canonical_index + 1);
  CheckMaxCanonicalIndex();
  CanonicalType& canonical_type = group.type;
  // Compute the canonical index of the supertype: If it is relative, we
  // need to add {first_canonical_index}.
  canonical_supertypes_[first_canonical_index] =
      canonical_type.is_relative_supertype
          ? canonical_type.type_def.supertype + first_canonical_index
          : canonical_type.type_def.supertype;
  module->isorecursive_canonical_type_ids[start_index] = first_canonical_index;
  // Check that this canonical ID is not used yet.
  DCHECK(std::none_of(
      canonical_singleton_groups_.begin(), canonical_singleton_groups_.end(),
      [=](auto& entry) { return entry.second == first_canonical_index; }));
  DCHECK(std::none_of(
      canonical_groups_.begin(), canonical_groups_.end(),
      [=](auto& entry) { return entry.second == first_canonical_index; }));
  canonical_singleton_groups_.emplace(group, first_canonical_index);
}

uint32_t TypeCanonicalizer::AddRecursiveGroup(const FunctionSig* sig) {
  base::MutexGuard mutex_guard(&mutex_);
// Types in the signature must be module-independent.
#if DEBUG
  for (ValueType type : sig->all()) DCHECK(!type.has_index());
#endif
  CanonicalSingletonGroup group;
  const bool is_final = true;
  const bool is_shared = false;
  group.type.type_def = TypeDefinition(sig, kNoSuperType, is_final, is_shared);
  group.type.is_relative_supertype = false;
  const FunctionSig* existing_sig = nullptr;
  int canonical_index = FindCanonicalGroup(group, &existing_sig);
  if (canonical_index >= 0) {
    // Make sure this signature can be looked up later.
    auto it = canonical_sigs_.find(canonical_index);
    if (it == canonical_sigs_.end()) {
      DCHECK_NOT_NULL(existing_sig);
      canonical_sigs_[canonical_index] = existing_sig;
    }
    return canonical_index;
  }
  static_assert(kMaxCanonicalTypes <= kMaxInt);
  canonical_index = static_cast<int>(canonical_supertypes_.size());
  // We need to copy the signature in the local zone, or else we risk
  // storing a dangling pointer in the future.
  // TODO(clemensb): This leaks memory if duplicate types are added.
  auto builder =
      FunctionSig::Builder(&zone_, sig->return_count(), sig->parameter_count());
  for (auto type : sig->returns()) builder.AddReturn(type);
  for (auto type : sig->parameters()) builder.AddParam(type);
  const FunctionSig* allocated_sig = builder.Get();
  group.type.type_def =
      TypeDefinition(allocated_sig, kNoSuperType, is_final, is_shared);
  group.type.is_relative_supertype = false;
  canonical_singleton_groups_.emplace(group, canonical_index);
  canonical_supertypes_.emplace_back(kNoSuperType);
  DCHECK(!canonical_sigs_.contains(canonical_index));
  canonical_sigs_[canonical_index] = allocated_sig;
  CheckMaxCanonicalIndex();
  return canonical_index;
}

// It's only valid to call this for signatures that were previously registered
// with {AddRecursiveGroup(const FunctionSig* sig)}.
const FunctionSig* TypeCanonicalizer::LookupSignature(
    uint32_t canonical_index) const {
  base::MutexGuard mutex_guard(&mutex_);
  auto it = canonical_sigs_.find(canonical_index);
  CHECK(it != canonical_sigs_.end());
  return it->second;
}

void TypeCanonicalizer::AddPredefinedArrayTypes() {
  static constexpr std::pair<uint32_t, ValueType> kPredefinedArrayTypes[] = {
      {kPredefinedArrayI8Index, kWasmI8}, {kPredefinedArrayI16Index, kWasmI16}};
  for (auto [index, element_type] : kPredefinedArrayTypes) {
    DCHECK_EQ(index, canonical_singleton_groups_.size());
    CanonicalSingletonGroup group;
    static constexpr bool kMutable = true;
    // TODO(jkummerow): Decide whether this should be final or nonfinal.
    static constexpr bool kFinal = true;
    static constexpr bool kShared = false;  // TODO(14616): Fix this.
    ArrayType* type = zone_.New<ArrayType>(element_type, kMutable);
    group.type.type_def = TypeDefinition(type, kNoSuperType, kFinal, kShared);
    group.type.is_relative_supertype = false;
    canonical_singleton_groups_.emplace(group, index);
    canonical_supertypes_.emplace_back(kNoSuperType);
    DCHECK_LE(canonical_supertypes_.size(), kMaxCanonicalTypes);
  }
}

ValueType TypeCanonicalizer::CanonicalizeValueType(
    const WasmModule* module, ValueType type,
    uint32_t recursive_group_start) const {
  if (!type.has_index()) return type;
  static_assert(kMaxCanonicalTypes <= (1u << ValueType::kHeapTypeBits));
  return type.ref_index() >= recursive_group_start
             ? ValueType::CanonicalWithRelativeIndex(
                   type.kind(), type.ref_index() - recursive_group_start)
             : ValueType::FromIndex(
                   type.kind(),
                   module->isorecursive_canonical_type_ids[type.ref_index()]);
}

bool TypeCanonicalizer::IsCanonicalSubtype(uint32_t canonical_sub_index,
                                           uint32_t canonical_super_index) {
  // Multiple threads could try to register and access recursive groups
  // concurrently.
  // TODO(manoskouk): Investigate if we can improve this synchronization.
  base::MutexGuard mutex_guard(&mutex_);
  while (canonical_sub_index != kNoSuperType) {
    if (canonical_sub_index == canonical_super_index) return true;
    canonical_sub_index = canonical_supertypes_[canonical_sub_index];
  }
  return false;
}

bool TypeCanonicalizer::IsCanonicalSubtype(uint32_t sub_index,
                                           uint32_t super_index,
                                           const WasmModule* sub_module,
                                           const WasmModule* super_module) {
  uint32_t canonical_super =
      super_module->isorecursive_canonical_type_ids[super_index];
  uint32_t canonical_sub =
      sub_module->isorecursive_canonical_type_ids[sub_index];
  return IsCanonicalSubtype(canonical_sub, canonical_super);
}

void TypeCanonicalizer::EmptyStorageForTesting() {
  base::MutexGuard mutex_guard(&mutex_);
  canonical_supertypes_.clear();
  canonical_groups_.clear();
  canonical_singleton_groups_.clear();
  zone_.Reset();
  AddPredefinedArrayTypes();
}

TypeCanonicalizer::CanonicalType TypeCanonicalizer::CanonicalizeTypeDef(
    const WasmModule* module, TypeDefinition type,
    uint32_t recursive_group_start) {
  uint32_t canonical_supertype = kNoSuperType;
  bool is_relative_supertype = false;
  if (type.supertype < recursive_group_start) {
    canonical_supertype =
        module->isorecursive_canonical_type_ids[type.supertype];
  } else if (type.supertype != kNoSuperType) {
    canonical_supertype = type.supertype - recursive_group_start;
    is_relative_supertype = true;
  }
  TypeDefinition result;
  switch (type.kind) {
    case TypeDefinition::kFunction: {
      const FunctionSig* original_sig = type.function_sig;
      FunctionSig::Builder builder(&zone_, original_sig->return_count(),
                                   original_sig->parameter_count());
      for (ValueType ret : original_sig->returns()) {
        builder.AddReturn(
            CanonicalizeValueType(module, ret, recursive_group_start));
      }
      for (ValueType param : original_sig->parameters()) {
        builder.AddParam(
            CanonicalizeValueType(module, param, recursive_group_start));
      }
      result = TypeDefinition(builder.Build(), canonical_supertype,
                              type.is_final, type.is_shared);
      break;
    }
    case TypeDefinition::kStruct: {
      const StructType* original_type = type.struct_type;
      StructType::Builder builder(&zone_, original_type->field_count());
      for (uint32_t i = 0; i < original_type->field_count(); i++) {
        builder.AddField(CanonicalizeValueType(module, original_type->field(i),
                                               recursive_group_start),
                         original_type->mutability(i),
                         original_type->field_offset(i));
      }
      builder.set_total_fields_size(original_type->total_fields_size());
      result = TypeDefinition(
          builder.Build(StructType::Builder::kUseProvidedOffsets),
          canonical_supertype, type.is_final, type.is_shared);
      break;
    }
    case TypeDefinition::kArray: {
      ValueType element_type = CanonicalizeValueType(
          module, type.array_type->element_type(), recursive_group_start);
      result = TypeDefinition(
          zone_.New<ArrayType>(element_type, type.array_type->mutability()),
          canonical_supertype, type.is_final, type.is_shared);
      break;
    }
  }

  return {result, is_relative_supertype};
}

// Returns the index of the canonical representative of the first type in this
// group, or -1 if an identical group does not exist.
int TypeCanonicalizer::FindCanonicalGroup(const CanonicalGroup& group) const {
  // Groups of size 0 do not make sense here; groups of size 1 should use
  // {CanonicalSingletonGroup} (see below).
  DCHECK_LT(1, group.types.size());
  auto it = canonical_groups_.find(group);
  return it == canonical_groups_.end() ? -1 : it->second;
}

// Returns the canonical index of the given group if it already exists.
// Optionally returns the FunctionSig* providing the type definition if
// the type in the group is a function type.
int TypeCanonicalizer::FindCanonicalGroup(const CanonicalSingletonGroup& group,
                                          const FunctionSig** out_sig) const {
  auto it = canonical_singleton_groups_.find(group);
  static_assert(kMaxCanonicalTypes <= kMaxInt);
  if (it == canonical_singleton_groups_.end()) return -1;
  if (out_sig) {
    const CanonicalSingletonGroup& found = it->first;
    if (found.type.type_def.kind == TypeDefinition::kFunction) {
      *out_sig = found.type.type_def.function_sig;
    }
  }
  return it->second;
}

size_t TypeCanonicalizer::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(TypeCanonicalizer, 296);
  // The storage of the canonical group's types is accounted for via the
  // allocator below (which tracks the zone memory).
  base::MutexGuard mutex_guard(&mutex_);
  size_t result = ContentSize(canonical_supertypes_);
  result += ContentSize(canonical_groups_);
  result += ContentSize(canonical_singleton_groups_);
  result += ContentSize(canonical_sigs_);
  result += allocator_.GetCurrentMemoryUsage();
  if (v8_flags.trace_wasm_offheap_memory) {
    PrintF("TypeCanonicalizer: %zu\n", result);
  }
  return result;
}

size_t TypeCanonicalizer::GetCurrentNumberOfTypes() const {
  base::MutexGuard mutex_guard(&mutex_);
  return canonical_supertypes_.size();
}

}  // namespace v8::internal::wasm
