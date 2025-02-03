// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/canonical-types.h"
#include "src/wasm/wasm-subtyping.h"
#include "test/common/flag-utils.h"
#include "test/common/wasm/flag-utils.h"
#include "test/unittests/test-utils.h"

namespace v8::internal::wasm::subtyping_unittest {

class WasmSubtypingTest : public TestWithPlatform {};
using FieldInit = std::pair<ValueType, bool>;

constexpr ValueType ref(uint32_t index) { return ValueType::Ref(index); }
constexpr ValueType refNull(uint32_t index) {
  return ValueType::RefNull(index);
}

FieldInit mut(ValueType type) { return FieldInit(type, true); }
FieldInit immut(ValueType type) { return FieldInit(type, false); }

void DefineStruct(WasmModule* module, std::initializer_list<FieldInit> fields,
                  uint32_t supertype = kNoSuperType, bool is_final = false,
                  bool is_shared = false, bool in_singleton_rec_group = true) {
  StructType::Builder builder(&module->signature_zone,
                              static_cast<uint32_t>(fields.size()));
  for (FieldInit field : fields) {
    builder.AddField(field.first, field.second);
  }
  module->AddStructTypeForTesting(builder.Build(), supertype, is_final,
                                  is_shared);
  if (in_singleton_rec_group) {
    GetTypeCanonicalizer()->AddRecursiveSingletonGroup(module);
  }
}

void DefineArray(WasmModule* module, FieldInit element_type,
                 uint32_t supertype = kNoSuperType, bool is_final = false,
                 bool is_shared = false, bool in_singleton_rec_group = true) {
  module->AddArrayTypeForTesting(module->signature_zone.New<ArrayType>(
                                     element_type.first, element_type.second),
                                 supertype, is_final, is_shared);
  if (in_singleton_rec_group) {
    GetTypeCanonicalizer()->AddRecursiveSingletonGroup(module);
  }
}

void DefineSignature(WasmModule* module,
                     std::initializer_list<ValueType> params,
                     std::initializer_list<ValueType> returns,
                     uint32_t supertype = kNoSuperType, bool is_final = false,
                     bool is_shared = false,
                     bool in_singleton_rec_group = true) {
  module->AddSignatureForTesting(
      FunctionSig::Build(&module->signature_zone, returns, params), supertype,
      is_final, is_shared);
  if (in_singleton_rec_group) {
    GetTypeCanonicalizer()->AddRecursiveGroup(module, 1);
  }
}

TEST_F(WasmSubtypingTest, Subtyping) {
  v8::internal::AccountingAllocator allocator;
  WasmModule module1_;
  WasmModule module2_;

  WasmModule* module1 = &module1_;
  WasmModule* module2 = &module2_;

  // Set up two identical modules.
  for (WasmModule* module : {module1, module2}) {
    /*  0 */ DefineStruct(module, {mut(ref(2)), immut(refNull(2))});
    /*  1 */ DefineStruct(module, {mut(ref(2)), immut(ref(2))}, 0);
    /*  2 */ DefineArray(module, immut(ref(0)));
    /*  3 */ DefineArray(module, immut(ref(1)), 2);
    /*  4 */ DefineStruct(module, {mut(ref(2)), immut(ref(3)), immut(kWasmF64)},
                          1);
    /*  5 */ DefineStruct(module, {mut(refNull(2)), immut(ref(2))});
    /*  6 */ DefineArray(module, mut(kWasmI32));
    /*  7 */ DefineArray(module, immut(kWasmI32));
    /*  8 */ DefineStruct(module, {mut(kWasmI32), immut(refNull(8))});
    /*  9 */ DefineStruct(module, {mut(kWasmI32), immut(refNull(8))}, 8);
    /* 10 */ DefineSignature(module, {}, {});
    /* 11 */ DefineSignature(module, {kWasmI32}, {kWasmI32});
    /* 12 */ DefineSignature(module, {kWasmI32, kWasmI32}, {kWasmI32});
    /* 13 */ DefineSignature(module, {ref(1)}, {kWasmI32});
    /* 14 */ DefineSignature(module, {ref(0)}, {kWasmI32}, 13);
    /* 15 */ DefineSignature(module, {ref(0)}, {ref(0)});
    /* 16 */ DefineSignature(module, {ref(0)}, {ref(4)}, 15);
    /* 17 */ DefineStruct(module, {mut(kWasmI32), immut(refNull(17))});

    // Rec. group.
    /* 18 */ DefineStruct(module, {mut(kWasmI32), immut(refNull(17))}, 17,
                          false, false, false);
    /* 19 */ DefineArray(module, {mut(refNull(21))}, kNoSuperType, false, false,
                         false);
    /* 20 */ DefineSignature(module, {kWasmI32}, {kWasmI32}, kNoSuperType,
                             false, false, false);
    /* 21 */ DefineSignature(module, {kWasmI32}, {kWasmI32}, 20, false, false,
                             false);
    GetTypeCanonicalizer()->AddRecursiveGroup(module, 4);

    // Identical rec. group.
    /* 22 */ DefineStruct(module, {mut(kWasmI32), immut(refNull(17))}, 17,
                          false, false, false);
    /* 23 */ DefineArray(module, {mut(refNull(25))}, kNoSuperType, false, false,
                         false);
    /* 24 */ DefineSignature(module, {kWasmI32}, {kWasmI32}, kNoSuperType,
                             false, false, false);
    /* 25 */ DefineSignature(module, {kWasmI32}, {kWasmI32}, 24, false, false,
                             false);
    GetTypeCanonicalizer()->AddRecursiveGroup(module, 4);

    // Nonidentical rec. group: the last function extends a type outside the
    // recursive group.
    /* 26 */ DefineStruct(module, {mut(kWasmI32), immut(refNull(17))}, 17,
                          false, false, false);
    /* 27 */ DefineArray(module, {mut(refNull(29))}, kNoSuperType, false, false,
                         false);
    /* 28 */ DefineSignature(module, {kWasmI32}, {kWasmI32}, kNoSuperType,
                             false, false, false);
    /* 29 */ DefineSignature(module, {kWasmI32}, {kWasmI32}, 20, false, false,
                             false);
    GetTypeCanonicalizer()->AddRecursiveGroup(module, 4);

    /* 30 */ DefineStruct(module, {mut(kWasmI32), immut(refNull(18))}, 18);
    /* 31 */ DefineStruct(
        module, {mut(ref(2)), immut(refNull(2)), immut(kWasmS128)}, 1);

    // Final types
    /* 32 */ DefineStruct(module, {mut(kWasmI32)}, kNoSuperType, true);
    /* 33 */ DefineStruct(module, {mut(kWasmI32), mut(kWasmI64)}, 32, true);
    /* 34 */ DefineStruct(module, {mut(kWasmI32)}, kNoSuperType, true);
    /* 35 */ DefineStruct(module, {mut(kWasmI32)}, kNoSuperType, false);

    // Shared types.
    /* 36 */ DefineStruct(module, {mut(kWasmI32)}, kNoSuperType);
    /* 37 */ DefineStruct(module, {mut(kWasmI32), mut(kWasmI64)}, 36);
    /* 38 */ DefineStruct(module, {mut(kWasmI32)}, kNoSuperType, false, true);
    /* 39 */ DefineStruct(module, {mut(kWasmI32), mut(kWasmI64)}, 38, false,
                          true);
    /* 40 */ DefineStruct(module, {mut(kWasmI32)}, kNoSuperType, false, true);
    /* 41 */ DefineSignature(module, {kWasmI32}, {kWasmI32}, false, true, true);
  }

  constexpr ValueType numeric_types[] = {kWasmI32, kWasmI64, kWasmF32, kWasmF64,
                                         kWasmS128};
  constexpr ValueType ref_types[] = {
      kWasmFuncRef,     kWasmEqRef,         kWasmStructRef,
      kWasmArrayRef,    kWasmI31Ref,        kWasmAnyRef,
      kWasmExternRef,   kWasmNullExternRef, kWasmNullRef,
      kWasmNullFuncRef, kWasmStringRef,     kWasmStringViewIter,
      kWasmExnRef,      kWasmNullExnRef,    kWasmRefNullExternString,
      refNull(0),   // struct
      ref(0),       // struct
      refNull(2),   // array
      ref(2),       // array
      refNull(11),  // signature
      ref(11)       // signature
  };

// Some macros to help managing types and modules.
#define SUBTYPE(type1, type2) \
  EXPECT_TRUE(IsSubtypeOf(type1, type2, module1, module))
#define SUBTYPE_IFF(type1, type2, condition) \
  EXPECT_EQ(IsSubtypeOf(type1, type2, module1, module), condition)
#define NOT_SUBTYPE(type1, type2) \
  EXPECT_FALSE(IsSubtypeOf(type1, type2, module1, module))
// Use only with indexed types.
#define VALID_SUBTYPE(type1, type2)                                        \
  EXPECT_TRUE(ValidSubtypeDefinition(type1.ref_index(), type2.ref_index(), \
                                     module1, module));                    \
  EXPECT_TRUE(IsSubtypeOf(type1, type2, module1, module));
#define NOT_VALID_SUBTYPE(type1, type2)                                     \
  EXPECT_FALSE(ValidSubtypeDefinition(type1.ref_index(), type2.ref_index(), \
                                      module1, module));
#define IDENTICAL(index1, index2)                         \
  EXPECT_TRUE(EquivalentTypes(ValueType::RefNull(index1), \
                              ValueType::RefNull(index2), module1, module));
#define DISTINCT(index1, index2)                           \
  EXPECT_FALSE(EquivalentTypes(ValueType::RefNull(index1), \
                               ValueType::RefNull(index2), module1, module));
// For union and intersection, we have a version that also checks the module,
// and one that does not.
#define UNION(type1, type2, type_result) \
  EXPECT_EQ(Union(type1, type2, module1, module).type, type_result)
#define UNION_M(type1, type2, type_result, module_result) \
  EXPECT_EQ(Union(type1, type2, module1, module),         \
            TypeInModule(type_result, module_result))
#define INTERSECTION(type1, type2, type_result) \
  EXPECT_EQ(Intersection(type1, type2, module1, module).type, type_result)
#define INTERSECTION_M(type1, type2, type_result, module_result) \
  EXPECT_EQ(Intersection(type1, type2, module1, module),         \
            TypeInModule(type_result, module_result))

  for (WasmModule* module : {module1, module2}) {
    // Type judgements across modules should work the same as within one module.

    // Value types are unrelated, except if they are equal.
    for (ValueType subtype : numeric_types) {
      for (ValueType supertype : numeric_types) {
        SUBTYPE_IFF(subtype, supertype, subtype == supertype);
      }
    }

    // Value types are unrelated with reference types.
    for (ValueType value_type : numeric_types) {
      for (ValueType ref_type : ref_types) {
        NOT_SUBTYPE(value_type, ref_type);
        NOT_SUBTYPE(ref_type, value_type);
      }
    }

    for (ValueType ref_type : ref_types) {
      const bool is_extern = ref_type == kWasmExternRef ||
                             ref_type == kWasmNullExternRef ||
                             ref_type == kWasmRefNullExternString;
      const bool is_any_func = ref_type == kWasmFuncRef ||
                               ref_type == kWasmNullFuncRef ||
                               ref_type == refNull(11) || ref_type == ref(11);
      const bool is_string_view = ref_type == kWasmStringViewIter ||
                                  ref_type == kWasmStringViewWtf8 ||
                                  ref_type == kWasmStringViewWtf16;
      const bool is_exn =
          ref_type == kWasmExnRef || ref_type == kWasmNullExnRef;
      SCOPED_TRACE("ref_type: " + ref_type.name());
      // Concrete reference types, i31ref, structref and arrayref are subtypes
      // of eqref, externref/funcref/anyref/exnref/functions are not.
      SUBTYPE_IFF(ref_type, kWasmEqRef,
                  ref_type != kWasmAnyRef && !is_any_func && !is_extern &&
                      !is_string_view && ref_type != kWasmStringRef && !is_exn);
      // Struct types are subtypes of structref.
      SUBTYPE_IFF(ref_type, kWasmStructRef,
                  ref_type == kWasmStructRef || ref_type == kWasmNullRef ||
                      ref_type == ref(0) || ref_type == refNull(0));
      // Array types are subtypes of arrayref.
      SUBTYPE_IFF(ref_type, kWasmArrayRef,
                  ref_type == kWasmArrayRef || ref_type == ref(2) ||
                      ref_type == kWasmNullRef || ref_type == refNull(2));
      // Functions are subtypes of funcref.
      SUBTYPE_IFF(ref_type, kWasmFuncRef, is_any_func);
      // Each reference type is a subtype of itself.
      SUBTYPE(ref_type, ref_type);
      // Each non-func, non-extern, non-string-view, non-string-iter reference
      // type is a subtype of anyref.
      SUBTYPE_IFF(ref_type, kWasmAnyRef,
                  !is_any_func && !is_extern && !is_string_view && !is_exn);
      // Only anyref is a subtype of anyref.
      SUBTYPE_IFF(kWasmAnyRef, ref_type, ref_type == kWasmAnyRef);
      // Only externref and nullexternref are subtypes of externref.
      SUBTYPE_IFF(ref_type, kWasmExternRef, is_extern);
      // Only nullexternref is a subtype of nullexternref.
      SUBTYPE_IFF(ref_type, kWasmNullExternRef, ref_type == kWasmNullExternRef);
      // Each nullable non-func, non-extern reference type is a supertype of
      // nullref.
      SUBTYPE_IFF(
          kWasmNullRef, ref_type,
          ref_type.is_nullable() && !is_any_func && !is_extern && !is_exn);
      // Only nullref is a subtype of nullref.
      SUBTYPE_IFF(ref_type, kWasmNullRef, ref_type == kWasmNullRef);
      // Only nullable funcs are supertypes of nofunc.
      SUBTYPE_IFF(kWasmNullFuncRef, ref_type,
                  ref_type.is_nullable() && is_any_func);
      // Only nullfuncref is a subtype of nullfuncref.
      SUBTYPE_IFF(ref_type, kWasmNullFuncRef, ref_type == kWasmNullFuncRef);

      // Make sure symmetric relations are symmetric.
      for (ValueType ref_type2 : ref_types) {
        if (ref_type == ref_type2) {
          EXPECT_TRUE(EquivalentTypes(ref_type, ref_type2, module, module1));
          EXPECT_TRUE(EquivalentTypes(ref_type2, ref_type, module1, module));
        } else {
          EXPECT_FALSE(EquivalentTypes(ref_type, ref_type2, module, module1));
          EXPECT_FALSE(EquivalentTypes(ref_type2, ref_type, module1, module));
        }
      }
    }

    // The rest of ref. types are unrelated.
    for (ValueType type_1 :
         {kWasmFuncRef, kWasmI31Ref, kWasmArrayRef, kWasmExnRef}) {
      for (ValueType type_2 :
           {kWasmFuncRef, kWasmI31Ref, kWasmArrayRef, kWasmExnRef}) {
        SUBTYPE_IFF(type_1, type_2, type_1 == type_2);
      }
    }

    // Unrelated refs are unrelated.
    NOT_VALID_SUBTYPE(ref(0), ref(2));
    NOT_VALID_SUBTYPE(refNull(3), refNull(1));
    // ref is a subtype of ref null for the same struct/array.
    VALID_SUBTYPE(ref(0), refNull(0));
    VALID_SUBTYPE(ref(2), refNull(2));
    // ref null is not a subtype of ref for the same struct/array.
    NOT_SUBTYPE(refNull(0), ref(0));
    NOT_SUBTYPE(refNull(2), ref(2));
    // ref is a subtype of ref null if the same is true for the underlying
    // structs/arrays.
    VALID_SUBTYPE(ref(3), refNull(2));
    // Prefix subtyping for structs.
    VALID_SUBTYPE(refNull(4), refNull(0));
    // Mutable fields are invariant.
    NOT_VALID_SUBTYPE(ref(0), ref(5));
    // Immutable fields are covariant.
    VALID_SUBTYPE(ref(1), ref(0));
    // Prefix subtyping + immutable field covariance for structs.
    VALID_SUBTYPE(refNull(4), refNull(1));
    // No subtyping between mutable/immutable fields.
    NOT_VALID_SUBTYPE(ref(7), ref(6));
    NOT_VALID_SUBTYPE(ref(6), ref(7));
    // Recursive types.
    VALID_SUBTYPE(ref(9), ref(8));

    // Identical rtts are subtypes of each other.
    SUBTYPE(ValueType::Rtt(5), ValueType::Rtt(5));
    // Rtts of unrelated types are unrelated.
    NOT_SUBTYPE(ValueType::Rtt(1), ValueType::Rtt(2));
    // Rtts of subtypes are not related.
    NOT_SUBTYPE(ValueType::Rtt(1), ValueType::Rtt(0));

    // Function subtyping;
    // Unrelated function types are unrelated.
    NOT_VALID_SUBTYPE(ref(10), ref(11));
    // Function type with different parameter counts are unrelated.
    NOT_VALID_SUBTYPE(ref(12), ref(11));
    // Parameter contravariance holds.
    VALID_SUBTYPE(ref(14), ref(13));
    // Return type covariance holds.
    VALID_SUBTYPE(ref(16), ref(15));
    // Identical types are subtype-related.
    VALID_SUBTYPE(ref(10), ref(10));
    VALID_SUBTYPE(ref(11), ref(11));

    // Canonicalization tests.

    // Groups should only be canonicalized to identical groups.
    IDENTICAL(18, 22);
    IDENTICAL(19, 23);
    IDENTICAL(20, 24);
    IDENTICAL(21, 25);

    DISTINCT(18, 26);
    DISTINCT(19, 27);
    DISTINCT(20, 28);
    DISTINCT(21, 29);

    // A type should not be canonicalized to an identical one with a different
    // group structure.
    DISTINCT(18, 17);

    // A subtype should also be subtype of an equivalent type.
    VALID_SUBTYPE(ref(30), ref(18));
    VALID_SUBTYPE(ref(30), ref(22));
    NOT_SUBTYPE(ref(30), ref(26));

    // Final types

    // A type is not a valid subtype of a final type.
    NOT_VALID_SUBTYPE(ref(33), ref(32));
    IDENTICAL(32, 34);
    // A final and a non-final type are distinct.
    DISTINCT(32, 35);

    /* Shared types */
    // A shared type can be a subtype of a shared type.
    VALID_SUBTYPE(ref(39), ref(38));
    // A shared type is not a valid subtype of a non-shared type and vice versa.
    NOT_VALID_SUBTYPE(ref(39), ref(36));
    NOT_VALID_SUBTYPE(ref(37), ref(38));
    // Two shared types are identical. A shared and non-shared type are
    // distinct.
    IDENTICAL(38, 40);
    DISTINCT(36, 38);
    // Abstract types
    SUBTYPE(ValueType::Ref(HeapType::kEqShared),
            ValueType::Ref(HeapType::kAnyShared));
    NOT_SUBTYPE(ValueType::Ref(HeapType::kEqShared),
                ValueType::Ref(HeapType::kAny));
    NOT_SUBTYPE(ValueType::Ref(HeapType::kEq),
                ValueType::Ref(HeapType::kAnyShared));
    NOT_SUBTYPE(ValueType::Ref(HeapType::kFuncShared),
                ValueType::Ref(HeapType::kAnyShared));
    SUBTYPE(ValueType::RefNull(HeapType::kNoneShared),
            ValueType::RefNull(HeapType::kI31Shared));
    SUBTYPE(ValueType::RefNull(HeapType::kNoFuncShared),
            ValueType::RefNull(HeapType::kFuncShared));
    SUBTYPE(ref(40), ValueType::RefNull(HeapType::kEqShared));
    SUBTYPE(ValueType::RefNull(HeapType::kNoneShared), refNull(40));
    NOT_SUBTYPE(ref(40), ValueType::RefNull(HeapType::kEq));
    NOT_SUBTYPE(ref(40), ValueType::RefNull(HeapType::kExternShared));
    SUBTYPE(ref(41), ValueType::RefNull(HeapType::kFuncShared));
    SUBTYPE(ValueType::RefNull(HeapType::kNoFuncShared), refNull(41));
    NOT_SUBTYPE(ref(41), ValueType::RefNull(HeapType::kAnyShared));
    NOT_SUBTYPE(ref(41), ValueType::RefNull(HeapType::kFunc));
    NOT_SUBTYPE(ref(0), ValueType::Ref(HeapType::kStructShared));
    NOT_SUBTYPE(ref(2), ValueType::Ref(HeapType::kArrayShared));
    NOT_SUBTYPE(ref(10), ValueType::Ref(HeapType::kFuncShared));

    // Rtts of identical types are subtype-related.
    SUBTYPE(ValueType::Rtt(8), ValueType::Rtt(17));

    // Unions and intersections.

    // Distinct numeric types are unrelated.
    for (ValueType type1 : numeric_types) {
      for (ValueType type2 : numeric_types) {
        UNION(type1, type2, (type1 == type2 ? type1 : kWasmBottom));
        INTERSECTION(type1, type2, (type1 == type2 ? type1 : kWasmBottom));
      }
    }
    // Numeric and reference types are unrelated.
    for (ValueType type1 : numeric_types) {
      for (ValueType type2 : ref_types) {
        UNION(type1, type2, kWasmBottom);
        INTERSECTION(type1, type2, kWasmBottom);
      }
    }

    // Reference type vs. itself and anyref.
    for (ValueType type : ref_types) {
      SCOPED_TRACE(type.name());
      UNION(type, type, type);
      INTERSECTION(type, type, type);
      if (type == kWasmStringViewIter || type == kWasmStringViewWtf8 ||
          type == kWasmStringViewWtf16) {
        // String views aren't subtypes of any nor supertypes of null.
        INTERSECTION(type, kWasmAnyRef, kWasmBottom);
        INTERSECTION(type, kWasmNullRef, kWasmBottom);
        continue;
      }
      if (type == kWasmFuncRef || type == kWasmNullFuncRef || type == ref(11) ||
          type == refNull(11) || type == kWasmExternRef ||
          type == kWasmNullExternRef || type == kWasmRefNullExternString) {
        // func and extern types don't share the same type hierarchy as anyref.
        INTERSECTION(type, kWasmAnyRef, kWasmBottom);
        continue;
      }
      bool is_exn = type == kWasmExnRef || type == kWasmNullExnRef;
      UNION(kWasmAnyRef, type, is_exn ? kWasmBottom : kWasmAnyRef);
      INTERSECTION(kWasmAnyRef, type, is_exn ? kWasmBottom : type);
      UNION(kWasmAnyRef.AsNonNull(), type,
            is_exn               ? kWasmBottom
            : type.is_nullable() ? kWasmAnyRef
                                 : kWasmAnyRef.AsNonNull());
      INTERSECTION(kWasmAnyRef.AsNonNull(), type,
                   is_exn                 ? kWasmBottom
                   : type != kWasmNullRef ? type.AsNonNull()
                                          : kWasmBottom);
    }

    // Abstract types vs abstract types.
    UNION(kWasmEqRef, kWasmStructRef, kWasmEqRef);
    UNION(kWasmEqRef, kWasmI31Ref, kWasmEqRef);
    UNION(kWasmEqRef, kWasmArrayRef, kWasmEqRef);
    UNION(kWasmEqRef, kWasmNullRef, kWasmEqRef);
    UNION(kWasmStructRef, kWasmI31Ref, kWasmEqRef);
    UNION(kWasmStructRef, kWasmArrayRef, kWasmEqRef);
    UNION(kWasmStructRef, kWasmNullRef, kWasmStructRef.AsNullable());
    UNION(kWasmI31Ref.AsNonNull(), kWasmArrayRef.AsNonNull(),
          kWasmEqRef.AsNonNull());
    UNION(kWasmI31Ref, kWasmNullRef, kWasmI31Ref.AsNullable());
    UNION(kWasmArrayRef, kWasmNullRef, kWasmArrayRef.AsNullable());
    UNION(kWasmStructRef.AsNonNull(), kWasmI31Ref.AsNonNull(),
          kWasmEqRef.AsNonNull());
    UNION(kWasmI31Ref.AsNonNull(), kWasmArrayRef, kWasmEqRef);
    UNION(kWasmAnyRef, kWasmNullRef, kWasmAnyRef);
    UNION(kWasmExternRef, kWasmNullExternRef, kWasmExternRef);
    UNION(kWasmRefNullExternString, kWasmNullExternRef,
          kWasmRefNullExternString);
    UNION(kWasmRefNullExternString.AsNonNull(), kWasmNullExternRef,
          kWasmRefNullExternString);
    UNION(kWasmRefNullExternString, kWasmExternRef, kWasmExternRef);
    UNION(kWasmRefNullExternString, kWasmAnyRef, kWasmBottom);
    UNION(kWasmRefNullExternString, kWasmFuncRef, kWasmBottom);
    // Imported strings and stringref represent the same values. Still, they are
    // in different type hierarchies and therefore incompatible (e.g. due to
    // different null representation).
    // (There is no interoperability between stringref and imported strings as
    // they are competing proposals.)
    UNION(kWasmRefNullExternString, kWasmStringRef, kWasmBottom);
    UNION(kWasmRefNullExternString.AsNonNull(), kWasmStringRef.AsNonNull(),
          kWasmBottom);
    UNION(kWasmFuncRef, kWasmNullFuncRef, kWasmFuncRef);
    UNION(kWasmFuncRef, kWasmStructRef, kWasmBottom);
    UNION(kWasmFuncRef, kWasmArrayRef, kWasmBottom);
    UNION(kWasmFuncRef, kWasmAnyRef, kWasmBottom);
    UNION(kWasmFuncRef, kWasmEqRef, kWasmBottom);
    UNION(kWasmStringRef, kWasmAnyRef, kWasmAnyRef);
    UNION(kWasmStringRef, kWasmStructRef, kWasmAnyRef);
    UNION(kWasmStringRef, kWasmArrayRef, kWasmAnyRef);
    UNION(kWasmStringRef, kWasmFuncRef, kWasmBottom);
    UNION(kWasmStringViewIter, kWasmStringRef, kWasmBottom);
    UNION(kWasmStringViewWtf8, kWasmStringRef, kWasmBottom);
    UNION(kWasmStringViewWtf16, kWasmStringRef, kWasmBottom);
    UNION(kWasmStringViewIter, kWasmAnyRef, kWasmBottom);
    UNION(kWasmStringViewWtf8, kWasmAnyRef, kWasmBottom);
    UNION(kWasmStringViewWtf16, kWasmAnyRef, kWasmBottom);
    UNION(kWasmNullFuncRef, kWasmEqRef, kWasmBottom);

    INTERSECTION(kWasmExternRef, kWasmEqRef, kWasmBottom);
    INTERSECTION(kWasmExternRef, kWasmStructRef, kWasmBottom);
    INTERSECTION(kWasmExternRef, kWasmI31Ref.AsNonNull(), kWasmBottom);
    INTERSECTION(kWasmExternRef, kWasmArrayRef, kWasmBottom);
    INTERSECTION(kWasmExternRef, kWasmNullRef, kWasmBottom);
    INTERSECTION(kWasmExternRef, kWasmFuncRef, kWasmBottom);
    INTERSECTION(kWasmNullExternRef, kWasmEqRef, kWasmBottom);
    INTERSECTION(kWasmNullExternRef, kWasmStructRef, kWasmBottom);
    INTERSECTION(kWasmNullExternRef, kWasmI31Ref, kWasmBottom);
    INTERSECTION(kWasmNullExternRef, kWasmArrayRef, kWasmBottom);
    INTERSECTION(kWasmNullExternRef, kWasmNullRef, kWasmBottom);
    INTERSECTION(kWasmNullExternRef, kWasmExternRef, kWasmNullExternRef);
    INTERSECTION(kWasmNullExternRef, kWasmExternRef.AsNonNull(), kWasmBottom);
    INTERSECTION(kWasmRefNullExternString, kWasmEqRef, kWasmBottom);
    INTERSECTION(kWasmRefNullExternString, kWasmAnyRef, kWasmBottom);
    INTERSECTION(kWasmRefNullExternString, kWasmFuncRef.AsNonNull(),
                 kWasmBottom);
    INTERSECTION(kWasmRefNullExternString, kWasmNullRef, kWasmBottom);
    INTERSECTION(kWasmRefNullExternString, kWasmNullExternRef,
                 kWasmNullExternRef);
    INTERSECTION(kWasmRefNullExternString.AsNonNull(), kWasmNullExternRef,
                 kWasmBottom);
    INTERSECTION(kWasmRefNullExternString, kWasmExternRef,
                 kWasmRefNullExternString);
    INTERSECTION(kWasmRefNullExternString, kWasmExternRef.AsNonNull(),
                 kWasmRefNullExternString.AsNonNull());

    INTERSECTION(kWasmFuncRef, kWasmEqRef, kWasmBottom);
    INTERSECTION(kWasmFuncRef, kWasmStructRef, kWasmBottom);
    INTERSECTION(kWasmFuncRef, kWasmI31Ref.AsNonNull(), kWasmBottom);
    INTERSECTION(kWasmFuncRef, kWasmArrayRef, kWasmBottom);
    INTERSECTION(kWasmFuncRef, kWasmNullRef, kWasmBottom);
    INTERSECTION(kWasmFuncRef, kWasmNullExternRef, kWasmBottom);
    INTERSECTION(kWasmNullFuncRef, kWasmEqRef, kWasmBottom);
    INTERSECTION(kWasmNullFuncRef, kWasmStructRef, kWasmBottom);
    INTERSECTION(kWasmNullFuncRef, kWasmI31Ref, kWasmBottom);
    INTERSECTION(kWasmNullFuncRef, kWasmArrayRef, kWasmBottom);
    INTERSECTION(kWasmNullFuncRef, kWasmNullRef, kWasmBottom);
    INTERSECTION(kWasmNullFuncRef, kWasmFuncRef, kWasmNullFuncRef);
    INTERSECTION(kWasmNullFuncRef, kWasmFuncRef.AsNonNull(), kWasmBottom);
    INTERSECTION(kWasmNullFuncRef, kWasmNullExternRef, kWasmBottom);

    INTERSECTION(kWasmEqRef, kWasmStructRef, kWasmStructRef);
    INTERSECTION(kWasmEqRef, kWasmI31Ref, kWasmI31Ref);
    INTERSECTION(kWasmEqRef, kWasmArrayRef, kWasmArrayRef);
    INTERSECTION(kWasmEqRef, kWasmNullRef, kWasmNullRef);
    INTERSECTION(kWasmEqRef, kWasmFuncRef, kWasmBottom);
    INTERSECTION(kWasmStructRef, kWasmI31Ref, kWasmNullRef);
    INTERSECTION(kWasmStructRef, kWasmArrayRef, kWasmNullRef);
    INTERSECTION(kWasmStructRef, kWasmNullRef, kWasmNullRef);
    INTERSECTION(kWasmI31Ref, kWasmArrayRef, kWasmNullRef);
    INTERSECTION(kWasmI31Ref.AsNonNull(), kWasmNullRef, kWasmBottom);
    INTERSECTION(kWasmArrayRef.AsNonNull(), kWasmNullRef, kWasmBottom);

    ValueType struct_type = ref(0);
    ValueType array_type = ref(2);
    ValueType function_type = ref(11);

    // Abstract vs indexed types.
    UNION(kWasmFuncRef, function_type, kWasmFuncRef);
    UNION(kWasmFuncRef, struct_type, kWasmBottom);
    UNION(kWasmFuncRef, array_type, kWasmBottom);
    INTERSECTION(kWasmFuncRef, struct_type, kWasmBottom);
    INTERSECTION(kWasmFuncRef, array_type, kWasmBottom);
    INTERSECTION_M(kWasmFuncRef, function_type, function_type, module);

    UNION(kWasmExnRef, struct_type, kWasmBottom);
    UNION(kWasmExnRef, array_type, kWasmBottom);
    UNION(kWasmExnRef, function_type, kWasmBottom);
    INTERSECTION(kWasmExnRef, struct_type, kWasmBottom);
    INTERSECTION(kWasmExnRef, array_type, kWasmBottom);
    INTERSECTION(kWasmExnRef, function_type, kWasmBottom);

    UNION(kWasmNullFuncRef, function_type, function_type.AsNullable());
    UNION(kWasmNullFuncRef, struct_type, kWasmBottom);
    UNION(kWasmNullFuncRef, array_type, kWasmBottom);
    INTERSECTION(kWasmNullFuncRef, struct_type, kWasmBottom);
    INTERSECTION(kWasmNullFuncRef, struct_type.AsNullable(), kWasmBottom);
    INTERSECTION(kWasmNullFuncRef, array_type, kWasmBottom);
    INTERSECTION(kWasmNullFuncRef, array_type.AsNullable(), kWasmBottom);
    INTERSECTION(kWasmNullFuncRef, function_type, kWasmBottom);
    INTERSECTION(kWasmNullFuncRef, function_type.AsNullable(),
                 kWasmNullFuncRef);

    UNION(kWasmEqRef, struct_type, kWasmEqRef);
    UNION(kWasmEqRef, array_type, kWasmEqRef);
    INTERSECTION(kWasmEqRef, struct_type, struct_type);
    INTERSECTION(kWasmEqRef, array_type, array_type);
    INTERSECTION(kWasmEqRef, function_type, kWasmBottom);

    UNION(kWasmStructRef, struct_type, kWasmStructRef);
    UNION(kWasmStructRef, array_type, kWasmEqRef);
    UNION(kWasmStructRef, function_type, kWasmBottom);
    INTERSECTION_M(kWasmStructRef, struct_type, struct_type, module);
    INTERSECTION(kWasmStructRef, array_type, kWasmBottom);
    INTERSECTION(kWasmStructRef, function_type, kWasmBottom);

    UNION(kWasmI31Ref, struct_type, kWasmEqRef);
    UNION(kWasmI31Ref, array_type, kWasmEqRef);
    INTERSECTION(kWasmI31Ref, struct_type, kWasmBottom);
    INTERSECTION(kWasmI31Ref, array_type, kWasmBottom);
    INTERSECTION(kWasmI31Ref, function_type, kWasmBottom);

    UNION(kWasmArrayRef, struct_type, kWasmEqRef);
    UNION(kWasmArrayRef, array_type, kWasmArrayRef);
    UNION(kWasmArrayRef, function_type, kWasmBottom);
    INTERSECTION(kWasmArrayRef, struct_type, kWasmBottom);
    INTERSECTION_M(kWasmArrayRef, array_type, array_type, module);
    INTERSECTION(kWasmArrayRef, function_type, kWasmBottom);

    UNION_M(kWasmNullRef, struct_type, struct_type.AsNullable(), module);
    UNION_M(kWasmNullRef, array_type, array_type.AsNullable(), module);
    UNION(kWasmNullRef, function_type, kWasmBottom);
    INTERSECTION(kWasmNullRef, struct_type, kWasmBottom);
    INTERSECTION(kWasmNullRef, array_type, kWasmBottom);
    INTERSECTION(kWasmNullRef, function_type, kWasmBottom);
    INTERSECTION(kWasmNullRef, struct_type.AsNullable(), kWasmNullRef);
    INTERSECTION(kWasmNullRef, array_type.AsNullable(), kWasmNullRef);
    INTERSECTION(kWasmNullRef, function_type.AsNullable(), kWasmBottom);

    UNION(struct_type, kWasmStringRef, kWasmAnyRef);
    UNION(array_type, kWasmStringRef, kWasmAnyRef);
    UNION(function_type, kWasmStringRef, kWasmBottom);

    UNION(struct_type, kWasmRefNullExternString, kWasmBottom);
    UNION(array_type, kWasmRefNullExternString, kWasmBottom);
    UNION(function_type, kWasmRefNullExternString, kWasmBottom);

    // Indexed types of different kinds.
    UNION(struct_type, array_type, kWasmEqRef.AsNonNull());
    INTERSECTION(struct_type, array_type, kWasmBottom);
    INTERSECTION(struct_type, function_type, kWasmBottom);
    INTERSECTION(array_type, function_type, kWasmBottom);

    // Nullable vs. non-nullable.
    UNION(struct_type, struct_type.AsNullable(), struct_type.AsNullable());
    INTERSECTION(struct_type, struct_type.AsNullable(), struct_type);
    UNION(kWasmStructRef, kWasmStructRef.AsNullable(),
          kWasmStructRef.AsNullable());
    INTERSECTION(kWasmStructRef, kWasmStructRef.AsNullable(), kWasmStructRef);

    // Concrete types of the same kind.
    // Subtyping relation.
    UNION_M(refNull(4), ref(1), refNull(1), module1);
    INTERSECTION_M(refNull(4), ref(1), ref(4), module1);
    INTERSECTION_M(refNull(1), refNull(4), refNull(4), module);
    // Common ancestor.
    UNION_M(ref(4), ref(31), ref(1), module1);
    INTERSECTION(ref(4), ref(31), kWasmBottom);
    // No common ancestor.
    UNION(ref(6), refNull(2), kWasmArrayRef.AsNullable());
    INTERSECTION(ref(6), refNull(2), kWasmBottom);
    UNION(ref(0), ref(17), kWasmStructRef.AsNonNull());
    INTERSECTION(ref(0), ref(17), kWasmBottom);
    UNION(ref(10), refNull(11), kWasmFuncRef);
    INTERSECTION(ref(10), refNull(11), kWasmBottom);

    // Shared types
    ValueType struct_shared = ref(40);
    ValueType function_shared = ref(41);
    UNION(struct_shared, struct_shared.AsNullable(),
          struct_shared.AsNullable());
    UNION(struct_shared, struct_type, kWasmBottom);
    UNION(struct_shared, function_shared, kWasmBottom);
    UNION(struct_shared, ValueType::Ref(HeapType::kI31Shared),
          ValueType::Ref(HeapType::kEqShared));
    UNION(struct_shared, ValueType::Ref(HeapType::kAnyShared),
          ValueType::Ref(HeapType::kAnyShared));
    UNION(struct_shared, ValueType::Ref(HeapType::kNoneShared), struct_shared);
    UNION(struct_shared, ValueType::Ref(HeapType::kAny), kWasmBottom);
    INTERSECTION(struct_shared, struct_shared.AsNullable(), struct_shared);
    INTERSECTION(struct_shared, struct_type, kWasmBottom);
    INTERSECTION(struct_shared, function_shared, kWasmBottom);
    INTERSECTION(struct_shared.AsNullable(),
                 ValueType::RefNull(HeapType::kI31Shared),
                 ValueType::RefNull(HeapType::kNoneShared));
    INTERSECTION(struct_shared, ValueType::Ref(HeapType::kAnyShared),
                 struct_shared);
    INTERSECTION(struct_shared.AsNullable(),
                 ValueType::RefNull(HeapType::kNoneShared),
                 ValueType::RefNull(HeapType::kNoneShared));
    INTERSECTION(struct_shared, ValueType::Ref(HeapType::kAny), kWasmBottom);
    UNION(function_shared, ValueType::Ref(HeapType::kFuncShared),
          ValueType::Ref(HeapType::kFuncShared));
    UNION(function_shared, ValueType::Ref(HeapType::kFunc), kWasmBottom);
    UNION(function_shared, ValueType::Ref(HeapType::kEqShared), kWasmBottom);
    UNION(function_shared, ValueType::Ref(HeapType::kNoFuncShared),
          function_shared);
    UNION(function_shared, ValueType::Ref(HeapType::kNoExternShared),
          kWasmBottom);
    INTERSECTION(function_shared, ValueType::Ref(HeapType::kFuncShared),
                 function_shared);
    INTERSECTION(function_shared, ValueType::Ref(HeapType::kFunc), kWasmBottom);
    INTERSECTION(function_shared, ValueType::Ref(HeapType::kEqShared),
                 kWasmBottom);
    INTERSECTION(function_shared.AsNullable(),
                 ValueType::RefNull(HeapType::kNoFuncShared),
                 ValueType::RefNull(HeapType::kNoFuncShared));
    INTERSECTION(function_shared, ValueType::Ref(HeapType::kNoExternShared),
                 kWasmBottom);
  }
#undef SUBTYPE
#undef NOT_SUBTYPE
#undef SUBTYPE_IFF
#undef VALID_SUBTYPE
#undef NOT_VALID_SUBTYPE
#undef IDENTICAL
#undef DISTINCT
#undef UNION
#undef UNION_M
#undef INTERSECTION
#undef INTERSECTION_M
}

}  // namespace v8::internal::wasm::subtyping_unittest
