// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_CANONICAL_TYPES_H_
#define V8_WASM_CANONICAL_TYPES_H_

#include <unordered_map>

#include "src/base/bounds.h"
#include "src/base/hashing.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-module.h"

namespace v8::internal::wasm {

class CanonicalTypeNamesProvider;

// We use ValueType instances constructed from canonical type indices, so we
// can't let them get bigger than what we have storage space for.
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
// Ensure that ValueType can hold all canonical type indexes.
static_assert(kMaxCanonicalTypes <= (1 << ValueType::kHeapTypeBits));

// A singleton class, responsible for isorecursive canonicalization of wasm
// types.
// A recursive group is a subsequence of types explicitly marked in the type
// section of a wasm module. Identical recursive groups have to be canonicalized
// to a single canonical group. Respective types in two identical groups are
// considered identical for all purposes.
// Two groups are considered identical if they have the same shape, and all
// type indices referenced in the same position in both groups reference:
// - identical types, if those do not belong to the rec. group,
// - types in the same relative position in the group, if those belong to the
//   rec. group.
class TypeCanonicalizer {
 public:
  static constexpr CanonicalTypeIndex kPredefinedArrayI8Index{0};
  static constexpr CanonicalTypeIndex kPredefinedArrayI16Index{1};
  static constexpr uint32_t kNumberOfPredefinedTypes = 2;

  TypeCanonicalizer();

  // Singleton class; no copying or moving allowed.
  TypeCanonicalizer(const TypeCanonicalizer& other) = delete;
  TypeCanonicalizer& operator=(const TypeCanonicalizer& other) = delete;
  TypeCanonicalizer(TypeCanonicalizer&& other) = delete;
  TypeCanonicalizer& operator=(TypeCanonicalizer&& other) = delete;

  // Register the last {size} types of {module} as a recursive group and
  // possibly canonicalize it if an identical one has been found.
  // Modifies {module->isorecursive_canonical_type_ids}.
  V8_EXPORT_PRIVATE void AddRecursiveGroup(WasmModule* module, uint32_t size);

  // Same as above, but for a group of size 1 (using the last type in the
  // module).
  V8_EXPORT_PRIVATE void AddRecursiveSingletonGroup(WasmModule* module);

  // Adds a module-independent signature as a recursive group, and canonicalizes
  // it if an identical is found. Returns the canonical index of the added
  // signature.
  V8_EXPORT_PRIVATE CanonicalTypeIndex
  AddRecursiveGroup(const FunctionSig* sig);

  // Retrieve back a type from a canonical index later.
  V8_EXPORT_PRIVATE const CanonicalSig* LookupFunctionSignature(
      CanonicalTypeIndex index) const;
  V8_EXPORT_PRIVATE const CanonicalStructType* LookupStruct(
      CanonicalTypeIndex index) const;
  V8_EXPORT_PRIVATE const CanonicalArrayType* LookupArray(
      CanonicalTypeIndex index) const;

  // Returns if {canonical_sub_index} is a canonical subtype of
  // {canonical_super_index}.
  V8_EXPORT_PRIVATE bool IsCanonicalSubtype(CanonicalTypeIndex sub_index,
                                            CanonicalTypeIndex super_index);

  // Returns if the type at {sub_index} in {sub_module} is a subtype of the
  // type at {super_index} in {super_module} after canonicalization.
  V8_EXPORT_PRIVATE bool IsCanonicalSubtype(ModuleTypeIndex sub_index,
                                            ModuleTypeIndex super_index,
                                            const WasmModule* sub_module,
                                            const WasmModule* super_module);

  // Deletes recursive groups. Used by fuzzers to avoid accumulating memory, and
  // used by specific tests e.g. for serialization / deserialization.
  V8_EXPORT_PRIVATE void EmptyStorageForTesting();

  size_t EstimateCurrentMemoryConsumption() const;

  V8_EXPORT_PRIVATE size_t GetCurrentNumberOfTypes() const;

  // Prepares wasm for the provided canonical type index. This reserves enough
  // space in the canonical rtts and the JSToWasm wrappers on the isolate roots.
  V8_EXPORT_PRIVATE static void PrepareForCanonicalTypeId(
      Isolate* isolate, CanonicalTypeIndex id);
  // Reset the canonical rtts and JSToWasm wrappers on the isolate roots for
  // testing purposes (in production cases canonical type ids are never freed).
  V8_EXPORT_PRIVATE static void ClearWasmCanonicalTypesForTesting(
      Isolate* isolate);

  V8_EXPORT_PRIVATE bool IsFunctionSignature(CanonicalTypeIndex index) const;
  V8_EXPORT_PRIVATE bool IsStruct(CanonicalTypeIndex index) const;
  V8_EXPORT_PRIVATE bool IsArray(CanonicalTypeIndex index) const;

  bool IsHeapSubtype(CanonicalValueType sub, CanonicalValueType super) const;
  bool IsCanonicalSubtype_Locked(CanonicalTypeIndex sub_index,
                                 CanonicalTypeIndex super_index) const;

  CanonicalTypeIndex FindIndex_Slow(const CanonicalSig* sig) const;

#if DEBUG
  // Check whether a supposedly-canonicalized function signature does indeed
  // live in this class's storage. Useful for guarding casts of signatures
  // that are entering the typed world.
  V8_EXPORT_PRIVATE bool Contains(const CanonicalSig* sig) const;
#endif

 private:
  struct CanonicalType {
    enum Kind : int8_t { kFunction, kStruct, kArray };

    union {
      const CanonicalSig* function_sig = nullptr;
      const CanonicalStructType* struct_type;
      const CanonicalArrayType* array_type;
    };
    CanonicalTypeIndex supertype{kNoSuperType};
    Kind kind = kFunction;
    bool is_final = false;
    bool is_shared = false;
    uint8_t subtyping_depth = 0;

    constexpr CanonicalType(const CanonicalSig* sig,
                            CanonicalTypeIndex supertype, bool is_final,
                            bool is_shared)
        : function_sig(sig),
          supertype(supertype),
          kind(kFunction),
          is_final(is_final),
          is_shared(is_shared) {}

    constexpr CanonicalType(const CanonicalStructType* type,
                            CanonicalTypeIndex supertype, bool is_final,
                            bool is_shared)
        : struct_type(type),
          supertype(supertype),
          kind(kStruct),
          is_final(is_final),
          is_shared(is_shared) {}

    constexpr CanonicalType(const CanonicalArrayType* type,
                            CanonicalTypeIndex supertype, bool is_final,
                            bool is_shared)
        : array_type(type),
          supertype(supertype),
          kind(kArray),
          is_final(is_final),
          is_shared(is_shared) {}

    constexpr CanonicalType() = default;
  };

  // Define the range of a recursion group; for use in {CanonicalHashing} and
  // {CanonicalEquality}.
  struct RecursionGroupRange {
    const CanonicalTypeIndex first;
    const CanonicalTypeIndex last;

    bool Contains(CanonicalTypeIndex index) const {
      return base::IsInRange(index.index, first.index, last.index);
    }
  };

  // Support for hashing of recursion groups, where type indexes have to be
  // hashed relative to the recursion group.
  struct CanonicalHashing {
    base::Hasher hasher;
    const RecursionGroupRange recgroup;

    explicit CanonicalHashing(RecursionGroupRange recgroup)
        : recgroup{recgroup} {}

    void Add(CanonicalType type) {
      // Add three pieces of information in a single number, for faster hashing:
      // Whether the type is final, whether the supertype index is relative
      // within the recgroup, and the supertype index itself.
      // For relative supertypes add {kMaxCanonicalTypes} to map those to a
      // separate index space (note that collisions in hashing are OK though).
      uint32_t is_relative = recgroup.Contains(type.supertype) ? 1 : 0;
      uint32_t supertype_index =
          type.supertype.index - is_relative * recgroup.first.index;
      static_assert(kMaxCanonicalTypes <= kMaxUInt32 >> 3);
      uint32_t metadata = (supertype_index << 3) | (type.is_shared << 2) |
                          (is_relative << 1) | (type.is_final << 0);
      hasher.Add(metadata);
      switch (type.kind) {
        case CanonicalType::kFunction:
          Add(*type.function_sig);
          break;
        case CanonicalType::kStruct:
          Add(*type.struct_type);
          break;
        case CanonicalType::kArray:
          Add(*type.array_type);
          break;
      }
    }

    void Add(CanonicalValueType value_type) {
      if (value_type.has_index() && recgroup.Contains(value_type.ref_index())) {
        // For relative indexed types add the kind and the relative index to the
        // hash. Shift the relative index by {kMaxCanonicalTypes} to map it to a
        // different index space (note that collisions in hashing are OK
        // though).
        static_assert(kMaxCanonicalTypes <= kMaxUInt32 / 2);
        hasher.Add(value_type.kind());
        hasher.Add((value_type.ref_index().index - recgroup.first.index) +
                   kMaxCanonicalTypes);
      } else {
        hasher.Add(value_type);
      }
    }

    void Add(const CanonicalSig& sig) {
      hasher.Add(sig.parameter_count());
      for (CanonicalValueType type : sig.all()) Add(type);
    }

    void Add(const CanonicalStructType& struct_type) {
      hasher.AddRange(struct_type.mutabilities());
      for (const ValueTypeBase& field : struct_type.fields()) {
        Add(CanonicalValueType{field});
      }
    }

    void Add(const CanonicalArrayType& array_type) {
      hasher.Add(array_type.mutability());
      Add(array_type.element_type());
    }

    size_t hash() const { return hasher.hash(); }
  };

  // Support for equality checking of recursion groups, where type indexes have
  // to be compared relative to their respective recursion group.
  struct CanonicalEquality {
    // Recursion group bounds for LHS and RHS.
    const RecursionGroupRange recgroup1;
    const RecursionGroupRange recgroup2;

    CanonicalEquality(RecursionGroupRange recgroup1,
                      RecursionGroupRange recgroup2)
        : recgroup1{recgroup1}, recgroup2{recgroup2} {}

    bool EqualTypeIndex(CanonicalTypeIndex index1,
                        CanonicalTypeIndex index2) const {
      const bool relative_index = recgroup1.Contains(index1);
      if (relative_index != recgroup2.Contains(index2)) return false;
      if (relative_index) {
        // Compare relative type indexes within the respective recgroups.
        uint32_t rel_type1 = index1.index - recgroup1.first.index;
        uint32_t rel_type2 = index2.index - recgroup2.first.index;
        if (rel_type1 != rel_type2) return false;
      } else if (index1 != index2) {
        return false;
      }
      return true;
    }

    bool EqualType(const CanonicalType& type1,
                   const CanonicalType& type2) const {
      if (!EqualTypeIndex(type1.supertype, type2.supertype)) return false;
      if (type1.is_final != type2.is_final) return false;
      if (type1.is_shared != type2.is_shared) return false;
      switch (type1.kind) {
        case CanonicalType::kFunction:
          return type2.kind == CanonicalType::kFunction &&
                 EqualSig(*type1.function_sig, *type2.function_sig);
        case CanonicalType::kStruct:
          return type2.kind == CanonicalType::kStruct &&
                 EqualStructType(*type1.struct_type, *type2.struct_type);
        case CanonicalType::kArray:
          return type2.kind == CanonicalType::kArray &&
                 EqualArrayType(*type1.array_type, *type2.array_type);
      }
    }

    bool EqualTypes(base::Vector<const CanonicalType> types1,
                    base::Vector<const CanonicalType> types2) const {
      return std::equal(types1.begin(), types1.end(), types2.begin(),
                        types2.end(),
                        std::bind_front(&CanonicalEquality::EqualType, this));
    }

    bool EqualValueType(CanonicalValueType type1,
                        CanonicalValueType type2) const {
      if (type1.kind() != type2.kind()) return false;
      const bool indexed = type1.has_index();
      if (indexed != type2.has_index()) return false;
      if (indexed) return EqualTypeIndex(type1.ref_index(), type2.ref_index());
      const bool is_ref = type1.is_object_reference();
      DCHECK_EQ(is_ref, type2.is_object_reference());
      if (is_ref &&
          type1.heap_representation() != type2.heap_representation()) {
        return false;
      }
      return true;
    }

    bool EqualSig(const CanonicalSig& sig1, const CanonicalSig& sig2) const {
      if (sig1.parameter_count() != sig2.parameter_count()) return false;
      return std::equal(
          sig1.all().begin(), sig1.all().end(), sig2.all().begin(),
          sig2.all().end(),
          std::bind_front(&CanonicalEquality::EqualValueType, this));
    }

    bool EqualStructType(const CanonicalStructType& type1,
                         const CanonicalStructType& type2) const {
      return
          // Compare fields, including a check that the size is the same.
          std::equal(
              type1.fields().begin(), type1.fields().end(),
              type2.fields().begin(), type2.fields().end(),
              std::bind_front(&CanonicalEquality::EqualValueType, this)) &&
          // Compare mutabilities, skipping the check for the size.
          std::equal(type1.mutabilities().begin(), type1.mutabilities().end(),
                     type2.mutabilities().begin());
    }

    bool EqualArrayType(const CanonicalArrayType& type1,
                        const CanonicalArrayType& type2) const {
      return type1.mutability() == type2.mutability() &&
             EqualValueType(type1.element_type(), type2.element_type());
    }
  };

  struct CanonicalGroup {
    CanonicalGroup(Zone* zone, size_t size, CanonicalTypeIndex first)
        : types(zone->AllocateVector<CanonicalType>(size)), first(first) {
      // size >= 2; otherwise a `CanonicalSingletonGroup` should have been used.
      DCHECK_LE(2, size);
    }

    bool operator==(const CanonicalGroup& other) const {
      CanonicalTypeIndex last{first.index +
                              static_cast<uint32_t>(types.size() - 1)};
      CanonicalTypeIndex other_last{
          other.first.index + static_cast<uint32_t>(other.types.size() - 1)};
      CanonicalEquality equality{{first, last}, {other.first, other_last}};
      return equality.EqualTypes(types, other.types);
    }

    size_t hash_value() const {
      CanonicalTypeIndex last{first.index +
                              static_cast<uint32_t>(types.size()) - 1};
      CanonicalHashing hasher{{first, last}};
      for (CanonicalType t : types) {
        hasher.Add(t);
      }
      return hasher.hash();
    }

    // The storage of this vector is the TypeCanonicalizer's zone_.
    const base::Vector<CanonicalType> types;
    const CanonicalTypeIndex first;
  };

  struct CanonicalSingletonGroup {
    bool operator==(const CanonicalSingletonGroup& other) const {
      CanonicalEquality equality{{index, index}, {other.index, other.index}};
      return equality.EqualType(type, other.type);
    }

    size_t hash_value() const {
      CanonicalHashing hasher{{index, index}};
      hasher.Add(type);
      return hasher.hash();
    }

    CanonicalType type;
    CanonicalTypeIndex index;
  };

  void AddPredefinedArrayTypes();

  CanonicalTypeIndex FindCanonicalGroup(const CanonicalGroup&) const;
  CanonicalTypeIndex FindCanonicalGroup(const CanonicalSingletonGroup&) const;

  // Canonicalize the module-specific type at `module_type_idx` within the
  // recursion group starting at `recursion_group_start`, using
  // `canonical_recgroup_start` as the start offset of types within the
  // recursion group.
  CanonicalType CanonicalizeTypeDef(
      const WasmModule* module, ModuleTypeIndex module_type_idx,
      ModuleTypeIndex recgroup_start,
      CanonicalTypeIndex canonical_recgroup_start);

  void CheckMaxCanonicalIndex() const;

  const CanonicalType* get(CanonicalTypeIndex index) const {
    return canonical_types_[index.index];
  }
  const CanonicalType* get(CanonicalValueType type) const {
    DCHECK(type.has_index());
    return get(type.ref_index());
  }

  bool IsShared(CanonicalValueType type) const;

  std::vector<CanonicalTypeIndex> canonical_supertypes_;
  // Set of all known canonical recgroups of size >=2.
  std::unordered_set<CanonicalGroup> canonical_groups_;
  // Set of all known canonical recgroups of size 1.
  std::unordered_set<CanonicalSingletonGroup> canonical_singleton_groups_;
  // Maps canonical indices back to the types.
  std::vector<const CanonicalType*> canonical_types_;
  AccountingAllocator allocator_;
  Zone zone_{&allocator_, "canonical type zone"};
  mutable base::SpinningMutex mutex_;
};

// Returns a reference to the TypeCanonicalizer shared by the entire process.
V8_EXPORT_PRIVATE TypeCanonicalizer* GetTypeCanonicalizer();

}  // namespace v8::internal::wasm

#endif  // V8_WASM_CANONICAL_TYPES_H_
