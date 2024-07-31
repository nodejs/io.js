// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_SUBTYPING_H_
#define V8_WASM_WASM_SUBTYPING_H_

#include "src/wasm/value-type.h"

namespace v8::internal::wasm {

struct WasmModule;

V8_NOINLINE V8_EXPORT_PRIVATE bool IsSubtypeOfImpl(
    ValueType subtype, ValueType supertype, const WasmModule* sub_module,
    const WasmModule* super_module);
V8_NOINLINE V8_EXPORT_PRIVATE bool IsHeapSubtypeOfImpl(
    HeapType sub_heap, HeapType super_heap, const WasmModule* sub_module,
    const WasmModule* super_module);

// Checks if type1, defined in module1, is equivalent with type2, defined in
// module2.
// Type equivalence (~) is described by the following rules:
// - Two numeric types are equivalent iff they are equal.
// - T(ht1) ~ T(ht2) iff ht1 ~ ht2 for T in {ref, ref null, rtt}.
// Equivalence of heap types ht1 ~ ht2 is defined as follows:
// - Two non-index heap types are equivalent iff they are equal.
// - Two indexed heap types are equivalent iff they are iso-recursive
//   equivalent.
V8_NOINLINE V8_EXPORT_PRIVATE bool EquivalentTypes(ValueType type1,
                                                   ValueType type2,
                                                   const WasmModule* module1,
                                                   const WasmModule* module2);

// Checks if {subtype}, defined in {module1}, is a subtype of {supertype},
// defined in {module2}.
// Subtyping between value types is described by the following rules
// (structural subtyping):
// - numeric types are subtype-related iff they are equal.
// - (ref null ht1) <: (ref null ht2) iff ht1 <: ht2.
// - (ref ht1) <: (ref null? ht2) iff ht1 <: ht2.
// - rtt1 <: rtt2 iff rtt1 ~ rtt2.
// For heap types, the following subtyping rules hold:
// - The abstract heap types form the following type hierarchies:
//
//                   any               func         extern
//               /        \             |             |
//             eq          \          nofunc       noextern
//          /   |   \       \
//       i31  array  struct  string
//          \___|______|_____/
//                  |
//                 none
//
// - All functions are subtypes of func.
// - All structs are subtypes of struct.
// - All arrays are subtypes of array.
// - An indexed heap type h1 is a subtype of indexed heap type h2 if h2 is
//   transitively an explicit canonical supertype of h1.
// Note that {any} includes references introduced by the host which belong to
// none of any's subtypes (e.g. JS objects).
V8_INLINE bool IsSubtypeOf(ValueType subtype, ValueType supertype,
                           const WasmModule* sub_module,
                           const WasmModule* super_module) {
  if (subtype == supertype && sub_module == super_module) return true;
  return IsSubtypeOfImpl(subtype, supertype, sub_module, super_module);
}

// Checks if {subtype} is a subtype of {supertype} (both defined in {module}).
V8_INLINE bool IsSubtypeOf(ValueType subtype, ValueType supertype,
                           const WasmModule* module) {
  // If the types are trivially identical, exit early.
  if (V8_LIKELY(subtype == supertype)) return true;
  return IsSubtypeOfImpl(subtype, supertype, module, module);
}

V8_INLINE bool TypesUnrelated(ValueType type1, ValueType type2,
                              const WasmModule* module1,
                              const WasmModule* module2) {
  return !IsSubtypeOf(type1, type2, module1, module2) &&
         !IsSubtypeOf(type2, type1, module2, module1);
}

V8_INLINE bool IsHeapSubtypeOf(HeapType subtype, HeapType supertype,
                               const WasmModule* sub_module,
                               const WasmModule* super_module) {
  if (subtype == supertype && sub_module == super_module) return true;
  return IsHeapSubtypeOfImpl(subtype, supertype, sub_module, super_module);
}

// Checks if {subtype} is a subtype of {supertype} (both defined in {module}).
V8_INLINE bool IsHeapSubtypeOf(HeapType subtype, HeapType supertype,
                               const WasmModule* module) {
  // If the types are trivially identical, exit early.
  if (V8_LIKELY(subtype == supertype)) return true;
  return IsHeapSubtypeOfImpl(subtype, supertype, module, module);
}

V8_INLINE bool HeapTypesUnrelated(HeapType heap1, HeapType heap2,
                                  const WasmModule* module1,
                                  const WasmModule* module2) {
  return !IsHeapSubtypeOf(heap1, heap2, module1, module2) &&
         !IsHeapSubtypeOf(heap2, heap1, module2, module1);
}

// Checks whether {subtype_index} is valid as a declared subtype of
// {supertype_index}.
// - Both type must be of the same kind (function, struct, or array).
// - Both type must have the same {is_shared} flag.
// - Structs: Subtype must have at least as many fields as supertype,
//   covariance for respective immutable fields, equivalence for respective
//   mutable fields.
// - Arrays: subtyping of respective element types for immutable arrays,
//   equivalence of element types for mutable arrays.
// - Functions: equal number of parameter and return types. Contravariance for
//   respective parameter types, covariance for respective return types.
V8_EXPORT_PRIVATE bool ValidSubtypeDefinition(uint32_t subtype_index,
                                              uint32_t supertype_index,
                                              const WasmModule* sub_module,
                                              const WasmModule* super_module);

V8_EXPORT_PRIVATE bool IsShared(ValueType type, const WasmModule* module);

struct TypeInModule {
  ValueType type;
  const WasmModule* module;

  TypeInModule(ValueType type, const WasmModule* module)
      : type(type), module(module) {}

  TypeInModule() : TypeInModule(kWasmBottom, nullptr) {}

  bool operator==(const TypeInModule& other) const {
    return type == other.type && module == other.module;
  }

  bool operator!=(const TypeInModule& other) const {
    return type != other.type || module != other.module;
  }
};

inline std::ostream& operator<<(std::ostream& oss, TypeInModule type) {
  return oss << type.type.name() << "@"
             << reinterpret_cast<intptr_t>(type.module);
}

// Returns {kWasmBottom} if the union of {type1} and {type2} is not defined.
V8_EXPORT_PRIVATE TypeInModule Union(ValueType type1, ValueType type2,
                                     const WasmModule* module1,
                                     const WasmModule* module2);

V8_INLINE V8_EXPORT_PRIVATE TypeInModule Union(TypeInModule type1,
                                               TypeInModule type2) {
  return Union(type1.type, type2.type, type1.module, type2.module);
}

V8_EXPORT_PRIVATE TypeInModule Intersection(ValueType type1, ValueType type2,
                                            const WasmModule* module1,
                                            const WasmModule* module2);

V8_INLINE V8_EXPORT_PRIVATE TypeInModule Intersection(TypeInModule type1,
                                                      TypeInModule type2) {
  return Intersection(type1.type, type2.type, type1.module, type2.module);
}

// Returns the matching abstract null type (none, nofunc, noextern).
ValueType ToNullSentinel(TypeInModule type);

// Returns if two types share the same type hierarchy (any, extern, funcref).
bool IsSameTypeHierarchy(HeapType type1, HeapType type2,
                         const WasmModule* module);

}  // namespace v8::internal::wasm

#endif  // V8_WASM_WASM_SUBTYPING_H_
