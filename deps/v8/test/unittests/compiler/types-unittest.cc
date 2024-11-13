// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/types.h"

#include <vector>

#include "src/base/strings.h"
#include "src/execution/isolate.h"
#include "src/heap/factory-inl.h"
#include "src/objects/objects.h"
#include "test/common/types-fuzz.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

// Testing auxiliaries (breaking the Type abstraction).

static bool IsInteger(double x) {
  return nearbyint(x) == x && !i::IsMinusZero(x);  // Allows for infinities.
}

using bitset = Type::bitset;

class TypesTest : public TestWithNativeContextAndZone {
 public:
  using TypeIterator = Types::TypeVector::iterator;
  using ValueIterator = Types::ValueVector::iterator;
  Types T;

  TypesTest()
      : TestWithNativeContextAndZone(),
        T(zone(), isolate(), isolate()->random_number_generator()) {}

  bool IsBitset(Type type) { return type.IsBitset(); }
  bool IsUnion(Type type) { return type.IsUnionForTesting(); }
  BitsetType::bitset AsBitset(Type type) { return type.AsBitsetForTesting(); }
  const UnionType* AsUnion(Type type) { return type.AsUnionForTesting(); }

  bool Equal(Type type1, Type type2) {
    return type1.Equals(type2) &&
           this->IsBitset(type1) == this->IsBitset(type2) &&
           this->IsUnion(type1) == this->IsUnion(type2) &&
           type1.NumConstants() == type2.NumConstants() &&
           (!this->IsBitset(type1) ||
            this->AsBitset(type1) == this->AsBitset(type2)) &&
           (!this->IsUnion(type1) ||
            this->AsUnion(type1)->LengthForTesting() ==
                this->AsUnion(type2)->LengthForTesting());
  }

  void CheckEqual(Type type1, Type type2) { CHECK(Equal(type1, type2)); }

  void CheckSub(Type type1, Type type2) {
    CHECK(type1.Is(type2));
    CHECK(!type2.Is(type1));
    if (this->IsBitset(type1) && this->IsBitset(type2)) {
      CHECK(this->AsBitset(type1) != this->AsBitset(type2));
    }
  }

  void CheckSubOrEqual(Type type1, Type type2) {
    CHECK(type1.Is(type2));
    if (this->IsBitset(type1) && this->IsBitset(type2)) {
      CHECK((this->AsBitset(type1) | this->AsBitset(type2)) ==
            this->AsBitset(type2));
    }
  }

  void CheckUnordered(Type type1, Type type2) {
    CHECK(!type1.Is(type2));
    CHECK(!type2.Is(type1));
    if (this->IsBitset(type1) && this->IsBitset(type2)) {
      CHECK(this->AsBitset(type1) != this->AsBitset(type2));
    }
  }

  void CheckOverlap(Type type1, Type type2) {
    CHECK(type1.Maybe(type2));
    CHECK(type2.Maybe(type1));
  }

  void CheckDisjoint(Type type1, Type type2) {
    CHECK(!type1.Is(type2));
    CHECK(!type2.Is(type1));
    CHECK(!type1.Maybe(type2));
    CHECK(!type2.Maybe(type1));
  }

  void IsSomeType() {
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type t = *it;
      CHECK_EQ(1, this->IsBitset(t) + t.IsHeapConstant() + t.IsRange() +
                      t.IsOtherNumberConstant() + this->IsUnion(t));
    }
  }

  void Bitset() {
    // None and Any are bitsets.
    CHECK(this->IsBitset(T.None));
    CHECK(this->IsBitset(T.Any));

    CHECK(bitset(0) == this->AsBitset(T.None));
    CHECK(bitset(0xFFFFFFFFFFFFFFFEu) == this->AsBitset(T.Any));

    // Union(T1, T2) is bitset for bitsets T1,T2
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type union12 = T.Union(type1, type2);
        CHECK(!(this->IsBitset(type1) && this->IsBitset(type2)) ||
              this->IsBitset(union12));
      }
    }

    // Intersect(T1, T2) is bitset for bitsets T1,T2
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type intersect12 = T.Intersect(type1, type2);
        CHECK(!(this->IsBitset(type1) && this->IsBitset(type2)) ||
              this->IsBitset(intersect12));
      }
    }

    // Union(T1, T2) is bitset if T2 is bitset and T1.Is(T2)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type union12 = T.Union(type1, type2);
        CHECK(!(this->IsBitset(type2) && type1.Is(type2)) ||
              this->IsBitset(union12));
      }
    }

    // Union(T1, T2) is bitwise disjunction for bitsets T1,T2
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type union12 = T.Union(type1, type2);
        if (this->IsBitset(type1) && this->IsBitset(type2)) {
          CHECK((this->AsBitset(type1) | this->AsBitset(type2)) ==
                this->AsBitset(union12));
        }
      }
    }

    // Intersect(T1, T2) is bitwise conjunction for bitsets T1,T2 (modulo None)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        if (this->IsBitset(type1) && this->IsBitset(type2)) {
          Type intersect12 = T.Intersect(type1, type2);
          bitset bits = this->AsBitset(type1) & this->AsBitset(type2);
          CHECK(bits == this->AsBitset(intersect12));
        }
      }
    }
  }

  void Constant() {
    // Constructor
    for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
      Handle<i::Object> value = *vt;
      Type type = T.Constant(value);
      CHECK(type.IsBitset() || type.IsHeapConstant() ||
            type.IsOtherNumberConstant() || type.IsRange());
    }

    // Value attribute
    for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
      Handle<i::Object> value = *vt;
      Type type = T.Constant(value);
      if (type.IsHeapConstant()) {
        CHECK(value.address() == type.AsHeapConstant()->Value().address());
      } else if (type.IsOtherNumberConstant()) {
        CHECK(IsHeapNumber(*value));
        CHECK(Object::NumberValue(*value) ==
              type.AsOtherNumberConstant()->Value());
      } else if (type.IsRange()) {
        CHECK(type.IsRange());
        double v = Object::NumberValue(*value);
        CHECK(v == type.AsRange()->Min() && v == type.AsRange()->Max());
      } else {
        CHECK(type.IsSingleton() || type.Is(Type::Hole()) ||
              type.Is(Type::String()));
      }
    }

    // Functionality & Injectivity: Constant(V1) = Constant(V2) iff V1 = V2
    for (ValueIterator vt1 = T.values.begin(); vt1 != T.values.end(); ++vt1) {
      for (ValueIterator vt2 = T.values.begin(); vt2 != T.values.end(); ++vt2) {
        Handle<i::Object> value1 = *vt1;
        Handle<i::Object> value2 = *vt2;
        Type type1 = T.Constant(value1);
        Type type2 = T.Constant(value2);
        if (type1.IsOtherNumberConstant() && type2.IsOtherNumberConstant()) {
          CHECK(Equal(type1, type2) ==
                (type1.AsOtherNumberConstant()->Value() ==
                 type2.AsOtherNumberConstant()->Value()));
        } else if (type1.IsRange() && type2.IsRange()) {
          CHECK(Equal(type1, type2) ==
                ((type1.AsRange()->Min() == type2.AsRange()->Min()) &&
                 (type1.AsRange()->Max() == type2.AsRange()->Max())));
        } else if (type1.IsSingleton() || type2.IsSingleton()) {
          CHECK(Equal(type1, type2) == (*value1 == *value2));
        }
      }
    }

    // Typing of numbers
    Factory* fac = isolate()->factory();
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(0))).Is(T.UnsignedSmall));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(1))).Is(T.UnsignedSmall));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(42)))
              .Equals(T.Range(42, 42)));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(0x3FFFFFFF)))
              .Is(T.UnsignedSmall));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(-1))).Is(T.Negative31));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(-0x3FFFFFFF)))
              .Is(T.Negative31));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(-0x40000000)))
              .Is(T.Negative31));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(0x40000000)))
              .Is(T.Unsigned31));
    CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(0x40000000)))
               .Is(T.Unsigned30));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(0x7FFFFFFF)))
              .Is(T.Unsigned31));
    CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(0x7FFFFFFF)))
               .Is(T.Unsigned30));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(-0x40000001)))
              .Is(T.Negative32));
    CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(-0x40000001)))
               .Is(T.Negative31));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(-0x7FFFFFFF)))
              .Is(T.Negative32));
    CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(-0x7FFFFFFF - 1)))
               .Is(T.Negative31));
    if (SmiValuesAre31Bits()) {
      CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(0x40000000)))
                 .Is(T.UnsignedSmall));
      CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(0x7FFFFFFF)))
                 .Is(T.UnsignedSmall));
      CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(-0x40000001)))
                 .Is(T.SignedSmall));
      CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(-0x7FFFFFFF - 1)))
                 .Is(T.SignedSmall));
    } else {
      CHECK(SmiValuesAre32Bits());
      CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(0x40000000)))
                .Is(T.UnsignedSmall));
      CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(0x7FFFFFFF)))
                .Is(T.UnsignedSmall));
      CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(-0x40000001)))
                .Is(T.SignedSmall));
      CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(-0x7FFFFFFF - 1)))
                .Is(T.SignedSmall));
    }
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(0x80000000u)))
              .Is(T.Unsigned32));
    CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(0x80000000u)))
               .Is(T.Unsigned31));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(0xFFFFFFFFu)))
              .Is(T.Unsigned32));
    CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(0xFFFFFFFFu)))
               .Is(T.Unsigned31));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(0xFFFFFFFFu + 1.0)))
              .Is(T.PlainNumber));
    CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(0xFFFFFFFFu + 1.0)))
               .Is(T.Integral32));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(-0x7FFFFFFF - 2.0)))
              .Is(T.PlainNumber));
    CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(-0x7FFFFFFF - 2.0)))
               .Is(T.Integral32));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(0.1))).Is(T.PlainNumber));
    CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(0.1))).Is(T.Integral32));
    CHECK(
        T.Constant(T.CanonicalHandle(fac->NewNumber(-10.1))).Is(T.PlainNumber));
    CHECK(
        !T.Constant(T.CanonicalHandle(fac->NewNumber(-10.1))).Is(T.Integral32));
    CHECK(
        T.Constant(T.CanonicalHandle(fac->NewNumber(10e60))).Is(T.PlainNumber));
    CHECK(
        !T.Constant(T.CanonicalHandle(fac->NewNumber(10e60))).Is(T.Integral32));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(-1.0 * 0.0)))
              .Is(T.MinusZero));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(V8_INFINITY)))
              .Is(T.PlainNumber));
    CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(V8_INFINITY)))
               .Is(T.Integral32));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(-V8_INFINITY)))
              .Is(T.PlainNumber));
    CHECK(!T.Constant(T.CanonicalHandle(fac->NewNumber(-V8_INFINITY)))
               .Is(T.Integral32));

    // Typing of Strings
    Handle<String> s1 = T.CanonicalHandle(fac->NewStringFromAsciiChecked("a"));
    CHECK(T.Constant(s1).Is(T.InternalizedString));
    const base::uc16 two_byte[1] = {0x2603};
    Handle<String> s2 = T.CanonicalHandle(fac->NewTwoByteInternalizedString(
        base::Vector<const base::uc16>(two_byte, 1),
        StringHasher::HashSequentialString<uint16_t>(two_byte, 1,
                                                     HashSeed(isolate()))));
    CHECK(T.Constant(s2).Is(T.InternalizedString));

    // Typing of special constants
    CHECK(T.Constant(fac->the_hole_value()).Equals(T.Hole));
    CHECK(T.Constant(fac->property_cell_hole_value()).Equals(T.Hole));
    CHECK(T.Constant(fac->hash_table_hole_value()).Equals(T.Hole));
    CHECK(T.Constant(fac->null_value()).Equals(T.Null));
    CHECK(T.Constant(fac->undefined_value()).Equals(T.Undefined));
    CHECK(T.Constant(fac->minus_zero_value()).Equals(T.MinusZero));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(-0.0)))
              .Equals(T.MinusZero));
    CHECK(T.Constant(fac->nan_value()).Equals(T.NaN));
    CHECK(T.Constant(T.CanonicalHandle(fac->NewNumber(
                         std::numeric_limits<double>::quiet_NaN())))
              .Equals(T.NaN));
  }

  void Range() {
    // Constructor
    for (ValueIterator i = T.integers.begin(); i != T.integers.end(); ++i) {
      for (ValueIterator j = T.integers.begin(); j != T.integers.end(); ++j) {
        double min = Object::NumberValue(**i);
        double max = Object::NumberValue(**j);
        if (min > max) std::swap(min, max);
        Type type = T.Range(min, max);
        CHECK(type.IsRange());
      }
    }

    // Range attributes
    for (ValueIterator i = T.integers.begin(); i != T.integers.end(); ++i) {
      for (ValueIterator j = T.integers.begin(); j != T.integers.end(); ++j) {
        double min = Object::NumberValue(**i);
        double max = Object::NumberValue(**j);
        if (min > max) std::swap(min, max);
        Type type = T.Range(min, max);
        CHECK(min == type.AsRange()->Min());
        CHECK(max == type.AsRange()->Max());
      }
    }

    // Functionality & Injectivity:
    // Range(min1, max1) = Range(min2, max2) <=> min1 = min2 /\ max1 = max2
    for (ValueIterator i1 = T.integers.begin(); i1 != T.integers.end(); ++i1) {
      for (ValueIterator j1 = i1; j1 != T.integers.end(); ++j1) {
        for (ValueIterator i2 = T.integers.begin(); i2 != T.integers.end();
             ++i2) {
          for (ValueIterator j2 = i2; j2 != T.integers.end(); ++j2) {
            double min1 = Object::NumberValue(**i1);
            double max1 = Object::NumberValue(**j1);
            double min2 = Object::NumberValue(**i2);
            double max2 = Object::NumberValue(**j2);
            if (min1 > max1) std::swap(min1, max1);
            if (min2 > max2) std::swap(min2, max2);
            Type type1 = T.Range(min1, max1);
            Type type2 = T.Range(min2, max2);
            CHECK(Equal(type1, type2) == (min1 == min2 && max1 == max2));
          }
        }
      }
    }
  }

  void MinMax() {
    // If b is regular numeric bitset, then Range(b.Min(), b.Max()).Is(b).
    // TODO(neis): Need to ignore representation for this to be true.
    /*
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      if (this->IsBitset(type) && type.Is(T.Number) &&
          !type.Is(T.None) && !type.Is(T.NaN)) {
        Type range = T.Range(
            isolate->factory()->NewNumber(type.Min()),
            isolate->factory()->NewNumber(type.Max()));
        CHECK(range.Is(type));
      }
    }
    */

    // If b is regular numeric bitset, then b.Min() and b.Max() are integers.
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      if (this->IsBitset(type) && type.Is(T.Number) && !type.Is(T.NaN)) {
        CHECK(IsInteger(type.Min()) && IsInteger(type.Max()));
      }
    }

    // If b1 and b2 are regular numeric bitsets with b1.Is(b2), then
    // b1.Min() >= b2.Min() and b1.Max() <= b2.Max().
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        if (this->IsBitset(type1) && type1.Is(type2) && type2.Is(T.Number) &&
            !type1.Is(T.NaN) && !type2.Is(T.NaN)) {
          CHECK(type1.Min() >= type2.Min());
          CHECK(type1.Max() <= type2.Max());
        }
      }
    }

    // Lub(Range(x,y)).Min() <= x and y <= Lub(Range(x,y)).Max()
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      if (type.IsRange()) {
        Type lub = type.BitsetLubForTesting();
        CHECK(lub.Min() <= type.Min() && type.Max() <= lub.Max());
      }
    }

    // Rangification: If T.Is(Range(-inf,+inf)) and T is inhabited, then
    // T.Is(Range(T.Min(), T.Max())).
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      CHECK(!type.Is(T.Integer) || type.IsNone() ||
            type.Is(T.Range(type.Min(), type.Max())));
    }
  }

  void BitsetGlb() {
    // Lower: (T->BitsetGlb()).Is(T)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      Type glb = type.BitsetGlbForTesting();
      CHECK(glb.Is(type));
    }

    // Greatest: If T1.IsBitset() and T1.Is(T2), then T1.Is(T2->BitsetGlb())
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type glb2 = type2.BitsetGlbForTesting();
        CHECK(!this->IsBitset(type1) || !type1.Is(type2) || type1.Is(glb2));
      }
    }

    // Monotonicity: T1.Is(T2) implies (T1->BitsetGlb()).Is(T2->BitsetGlb())
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type glb1 = type1.BitsetGlbForTesting();
        Type glb2 = type2.BitsetGlbForTesting();
        CHECK(!type1.Is(type2) || glb1.Is(glb2));
      }
    }
  }

  void BitsetLub() {
    // Upper: T.Is(T->BitsetLub())
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      Type lub = type.BitsetLubForTesting();
      CHECK(type.Is(lub));
    }

    // Least: If T2.IsBitset() and T1.Is(T2), then (T1->BitsetLub()).Is(T2)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type lub1 = type1.BitsetLubForTesting();
        CHECK(!this->IsBitset(type2) || !type1.Is(type2) || lub1.Is(type2));
      }
    }

    // Monotonicity: T1.Is(T2) implies (T1->BitsetLub()).Is(T2->BitsetLub())
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type lub1 = type1.BitsetLubForTesting();
        Type lub2 = type2.BitsetLubForTesting();
        CHECK(!type1.Is(type2) || lub1.Is(lub2));
      }
    }
  }

  void Is1() {
    // Least Element (Bottom): None.Is(T)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      CHECK(T.None.Is(type));
    }

    // Greatest Element (Top): T.Is(Any)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      CHECK(type.Is(T.Any));
    }

    // Bottom Uniqueness: T.Is(None) implies T = None
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      if (type.Is(T.None)) CheckEqual(type, T.None);
    }

    // Top Uniqueness: Any.Is(T) implies T = Any
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      if (T.Any.Is(type)) CheckEqual(type, T.Any);
    }

    // Reflexivity: T.Is(T)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      CHECK(type.Is(type));
    }

    // Transitivity: T1.Is(T2) and T2.Is(T3) implies T1.Is(T3)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          Type type1 = *it1;
          Type type2 = *it2;
          Type type3 = *it3;
          CHECK(!(type1.Is(type2) && type2.Is(type3)) || type1.Is(type3));
        }
      }
    }

    // Antisymmetry: T1.Is(T2) and T2.Is(T1) iff T1 = T2
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        CHECK((type1.Is(type2) && type2.Is(type1)) == Equal(type1, type2));
      }
    }

    // (In-)Compatibilities.
    for (TypeIterator i = T.types.begin(); i != T.types.end(); ++i) {
      for (TypeIterator j = T.types.begin(); j != T.types.end(); ++j) {
        Type type1 = *i;
        Type type2 = *j;
        CHECK(
            !type1.Is(type2) || this->IsBitset(type2) || this->IsUnion(type2) ||
            this->IsUnion(type1) ||
            (type1.IsHeapConstant() && type2.IsHeapConstant()) ||
            (this->IsBitset(type1) && type2.IsRange()) ||
            (type1.IsRange() && type2.IsRange()) ||
            (type1.IsOtherNumberConstant() && type2.IsOtherNumberConstant()) ||
            type1.IsNone());
      }
    }
  }

  void Is2() {
    // Range(X1, Y1).Is(Range(X2, Y2)) iff X1 >= X2 /\ Y1 <= Y2
    for (ValueIterator i1 = T.integers.begin(); i1 != T.integers.end(); ++i1) {
      for (ValueIterator j1 = i1; j1 != T.integers.end(); ++j1) {
        for (ValueIterator i2 = T.integers.begin(); i2 != T.integers.end();
             ++i2) {
          for (ValueIterator j2 = i2; j2 != T.integers.end(); ++j2) {
            double min1 = Object::NumberValue(**i1);
            double max1 = Object::NumberValue(**j1);
            double min2 = Object::NumberValue(**i2);
            double max2 = Object::NumberValue(**j2);
            if (min1 > max1) std::swap(min1, max1);
            if (min2 > max2) std::swap(min2, max2);
            Type type1 = T.Range(min1, max1);
            Type type2 = T.Range(min2, max2);
            CHECK(type1.Is(type2) == (min1 >= min2 && max1 <= max2));
          }
        }
      }
    }

    // Constant(V1).Is(Constant(V2)) iff V1 = V2
    for (ValueIterator vt1 = T.values.begin(); vt1 != T.values.end(); ++vt1) {
      for (ValueIterator vt2 = T.values.begin(); vt2 != T.values.end(); ++vt2) {
        Handle<i::Object> value1 = *vt1;
        Handle<i::Object> value2 = *vt2;
        Type const_type1 = T.Constant(value1);
        Type const_type2 = T.Constant(value2);
        if (const_type1.IsOtherNumberConstant() &&
            const_type2.IsOtherNumberConstant()) {
          CHECK(const_type1.Is(const_type2) ==
                (const_type1.AsOtherNumberConstant()->Value() ==
                 const_type2.AsOtherNumberConstant()->Value()));
        } else if (const_type1.IsRange() && const_type2.IsRange()) {
          CHECK(
              Equal(const_type1, const_type2) ==
              ((const_type1.AsRange()->Min() == const_type2.AsRange()->Min()) &&
               (const_type1.AsRange()->Max() == const_type2.AsRange()->Max())));
        } else if (const_type1.IsSingleton() && const_type2.IsSingleton()) {
          CHECK(const_type1.Is(const_type2) == (*value1 == *value2));
        }
      }
    }

    // Range-specific subtyping

    // Lub(Range(x,y)).Is(T.Union(T.Integral32, T.OtherNumber))
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      if (type.IsRange()) {
        Type lub = type.BitsetLubForTesting();
        CHECK(lub.Is(T.PlainNumber));
      }
    }

    // Subtyping between concrete basic types

    CheckUnordered(T.Boolean, T.Null);
    CheckUnordered(T.Undefined, T.Null);
    CheckUnordered(T.Boolean, T.Undefined);

    CheckSub(T.SignedSmall, T.Number);
    CheckSub(T.Signed32, T.Number);
    CheckSubOrEqual(T.SignedSmall, T.Signed32);
    CheckUnordered(T.SignedSmall, T.MinusZero);
    CheckUnordered(T.Signed32, T.Unsigned32);

    CheckSub(T.UniqueName, T.Name);
    CheckSub(T.String, T.Name);
    CheckSub(T.InternalizedString, T.String);
    CheckSub(T.InternalizedString, T.UniqueName);
    CheckSub(T.InternalizedString, T.Name);
    CheckSub(T.Symbol, T.UniqueName);
    CheckSub(T.Symbol, T.Name);
    CheckUnordered(T.String, T.UniqueName);
    CheckUnordered(T.String, T.Symbol);
    CheckUnordered(T.InternalizedString, T.Symbol);

    CheckSub(T.Object, T.Receiver);
    CheckSub(T.Proxy, T.Receiver);
    CheckSub(T.Array, T.Object);
    CheckSub(T.OtherObject, T.Object);
    CheckSub(T.OtherUndetectable, T.Object);

    CheckUnordered(T.Object, T.Proxy);
    CheckUnordered(T.Array, T.Undetectable);
    CheckUnordered(T.OtherObject, T.Undetectable);

    // Subtyping between concrete structural types

    CheckSub(T.SmiConstant, T.SignedSmall);
    CheckSub(T.SmiConstant, T.Signed32);
    CheckSub(T.SmiConstant, T.Number);
    CheckSub(T.ObjectConstant1, T.Object);
    CheckSub(T.ObjectConstant2, T.Object);
    CheckSub(T.ArrayConstant, T.Object);
    CheckSub(T.ArrayConstant, T.Array);
    CheckSub(T.ArrayConstant, T.Receiver);
    CheckSub(T.UninitializedConstant, T.Internal);
    CheckUnordered(T.ObjectConstant1, T.ObjectConstant2);
    CheckUnordered(T.ObjectConstant1, T.ArrayConstant);
    CheckUnordered(T.UninitializedConstant, T.Null);
    CheckUnordered(T.UninitializedConstant, T.Undefined);
  }

  void Maybe() {
    // T.Maybe(Any) iff T inhabited
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      CHECK(type.Maybe(T.Any) == !type.IsNone());
    }

    // T.Maybe(None) never
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      CHECK(!type.Maybe(T.None));
    }

    // Reflexivity upto Inhabitation: T.Maybe(T) iff T inhabited
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      CHECK(type.Maybe(type) == !type.IsNone());
    }

    // Symmetry: T1.Maybe(T2) iff T2.Maybe(T1)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        CHECK(type1.Maybe(type2) == type2.Maybe(type1));
      }
    }

    // T1.Maybe(T2) implies T1, T2 inhabited
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        CHECK(!type1.Maybe(type2) || (!type1.IsNone() && !type2.IsNone()));
      }
    }

    // T1.Maybe(T2) implies Intersect(T1, T2) inhabited
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type intersect12 = T.Intersect(type1, type2);
        CHECK(!type1.Maybe(type2) || !intersect12.IsNone());
      }
    }

    // T1.Is(T2) and T1 inhabited implies T1.Maybe(T2)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        CHECK(!(type1.Is(type2) && !type1.IsNone()) || type1.Maybe(type2));
      }
    }

    // Constant(V1).Maybe(Constant(V2)) iff V1 = V2
    for (ValueIterator vt1 = T.values.begin(); vt1 != T.values.end(); ++vt1) {
      for (ValueIterator vt2 = T.values.begin(); vt2 != T.values.end(); ++vt2) {
        Handle<i::Object> value1 = *vt1;
        Handle<i::Object> value2 = *vt2;
        Type const_type1 = T.Constant(value1);
        Type const_type2 = T.Constant(value2);
        if (const_type1.IsOtherNumberConstant() &&
            const_type2.IsOtherNumberConstant()) {
          CHECK(const_type1.Maybe(const_type2) ==
                (const_type1.AsOtherNumberConstant()->Value() ==
                 const_type2.AsOtherNumberConstant()->Value()));
        } else if (const_type1.IsRange() && const_type2.IsRange()) {
          CHECK(
              Equal(const_type1, const_type2) ==
              ((const_type1.AsRange()->Min() == const_type2.AsRange()->Min()) &&
               (const_type1.AsRange()->Max() == const_type2.AsRange()->Max())));
        } else if (const_type1.IsSingleton() && const_type2.IsSingleton()) {
          CHECK(const_type1.Maybe(const_type2) == (*value1 == *value2));
        }
      }
    }

    // Basic types
    CheckDisjoint(T.Boolean, T.Null);
    CheckDisjoint(T.Undefined, T.Null);
    CheckDisjoint(T.Boolean, T.Undefined);
    CheckOverlap(T.SignedSmall, T.Number);
    CheckOverlap(T.NaN, T.Number);
    CheckDisjoint(T.Signed32, T.NaN);
    CheckOverlap(T.UniqueName, T.Name);
    CheckOverlap(T.String, T.Name);
    CheckOverlap(T.InternalizedString, T.String);
    CheckOverlap(T.InternalizedString, T.UniqueName);
    CheckOverlap(T.InternalizedString, T.Name);
    CheckOverlap(T.Symbol, T.UniqueName);
    CheckOverlap(T.Symbol, T.Name);
    CheckOverlap(T.String, T.UniqueName);
    CheckDisjoint(T.String, T.Symbol);
    CheckDisjoint(T.InternalizedString, T.Symbol);
    CheckOverlap(T.Object, T.Receiver);
    CheckOverlap(T.OtherObject, T.Object);
    CheckOverlap(T.Proxy, T.Receiver);
    CheckDisjoint(T.Object, T.Proxy);

    // Structural types
    CheckOverlap(T.SmiConstant, T.SignedSmall);
    CheckOverlap(T.SmiConstant, T.Signed32);
    CheckOverlap(T.SmiConstant, T.Number);
    CheckOverlap(T.ObjectConstant1, T.Object);
    CheckOverlap(T.ObjectConstant2, T.Object);
    CheckOverlap(T.ArrayConstant, T.Object);
    CheckOverlap(T.ArrayConstant, T.Receiver);
    CheckOverlap(T.ObjectConstant1, T.ObjectConstant1);
    CheckDisjoint(T.ObjectConstant1, T.ObjectConstant2);
    CheckDisjoint(T.ObjectConstant1, T.ArrayConstant);
  }

  void Union1() {
    // Identity: Union(T, None) = T
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      Type union_type = T.Union(type, T.None);
      CheckEqual(union_type, type);
    }

    // Domination: Union(T, Any) = Any
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      Type union_type = T.Union(type, T.Any);
      CheckEqual(union_type, T.Any);
    }

    // Idempotence: Union(T, T) = T
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      Type union_type = T.Union(type, type);
      CheckEqual(union_type, type);
    }

    // Commutativity: Union(T1, T2) = Union(T2, T1)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type union12 = T.Union(type1, type2);
        Type union21 = T.Union(type2, type1);
        CheckEqual(union12, union21);
      }
    }

    // Associativity: Union(T1, Union(T2, T3)) = Union(Union(T1, T2), T3)
    // This does NOT hold!  For example:
    // (Unsigned32 \/ Range(0,5)) \/ Range(-5,0) = Unsigned32 \/ Range(-5,0)
    // Unsigned32 \/ (Range(0,5) \/ Range(-5,0)) = Unsigned32 \/ Range(-5,5)
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          Type type1 = *it1;
          Type type2 = *it2;
          Type type3 = *it3;
          Type union12 = T.Union(type1, type2);
          Type union23 = T.Union(type2, type3);
          Type union1_23 = T.Union(type1, union23);
          Type union12_3 = T.Union(union12, type3);
          CheckEqual(union1_23, union12_3);
        }
      }
    }
    */

    // Meet: T1.Is(Union(T1, T2)) and T2.Is(Union(T1, T2))
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type union12 = T.Union(type1, type2);
        CHECK(type1.Is(union12));
        CHECK(type2.Is(union12));
      }
    }

    // Upper Boundedness: T1.Is(T2) implies Union(T1, T2) = T2
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type union12 = T.Union(type1, type2);
        if (type1.Is(type2)) CheckEqual(union12, type2);
      }
    }

    // Monotonicity: T1.Is(T2) implies Union(T1, T3).Is(Union(T2, T3))
    // This does NOT hold.  For example:
    // Range(-5,-1) <= Signed32
    // Range(-5,-1) \/ Range(1,5) = Range(-5,5) </= Signed32 \/ Range(1,5)
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          Type type1 = *it1;
          Type type2 = *it2;
          Type type3 = *it3;
          Type union13 = T.Union(type1, type3);
          Type union23 = T.Union(type2, type3);
          CHECK(!type1.Is(type2) || union13.Is(union23));
        }
      }
    }
    */
  }

  void Union2() {
    // Monotonicity: T1.Is(T3) and T2.Is(T3) implies Union(T1, T2).Is(T3)
    // This does NOT hold.  For example:
    // Range(-2^33, -2^33) <= OtherNumber
    // Range(2^33, 2^33) <= OtherNumber
    // Range(-2^33, 2^33) </= OtherNumber
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          Type type1 = *it1;
          Type type2 = *it2;
          Type type3 = *it3;
          Type union12 = T.Union(type1, type2);
          CHECK(!(type1.Is(type3) && type2.Is(type3)) || union12.Is(type3));
        }
      }
    }
    */
  }

  void Union3() {
    // Monotonicity: T1.Is(T2) or T1.Is(T3) implies T1.Is(Union(T2, T3))
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      HandleScope scope(isolate());
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = it2; it3 != T.types.end(); ++it3) {
          Type type1 = *it1;
          Type type2 = *it2;
          Type type3 = *it3;
          Type union23 = T.Union(type2, type3);
          CHECK(!(type1.Is(type2) || type1.Is(type3)) || type1.Is(union23));
        }
      }
    }
  }

  void Union4() {
    // Constant-constant
    CheckSub(T.Union(T.ObjectConstant1, T.ObjectConstant2), T.Object);
    CheckOverlap(T.Union(T.ObjectConstant1, T.ArrayConstant), T.OtherObject);
    CheckOverlap(T.Union(T.ObjectConstant1, T.ArrayConstant), T.OtherObject);
    CheckDisjoint(T.Union(T.ObjectConstant1, T.ArrayConstant), T.Number);

    // Bitset-constant
    CheckSub(T.Union(T.ObjectConstant1, T.Signed32),
             T.Union(T.Object, T.Number));
    CheckSub(T.Union(T.ObjectConstant1, T.OtherObject), T.Object);
    CheckUnordered(T.Union(T.ObjectConstant1, T.String), T.OtherObject);
    CheckOverlap(T.Union(T.ObjectConstant1, T.String), T.Object);
    CheckDisjoint(T.Union(T.ObjectConstant1, T.String), T.Number);

    // Constant-union
    CheckEqual(T.Union(T.ObjectConstant1,
                       T.Union(T.ObjectConstant1, T.ObjectConstant2)),
               T.Union(T.ObjectConstant2, T.ObjectConstant1));
    CheckEqual(
        T.Union(T.Union(T.ArrayConstant, T.ObjectConstant2), T.ObjectConstant1),
        T.Union(T.ObjectConstant2,
                T.Union(T.ArrayConstant, T.ObjectConstant1)));

    // Union-union
    CheckEqual(T.Union(T.Union(T.ObjectConstant2, T.ObjectConstant1),
                       T.Union(T.ObjectConstant1, T.ObjectConstant2)),
               T.Union(T.ObjectConstant2, T.ObjectConstant1));
  }

  void Intersect() {
    // Identity: Intersect(T, Any) = T
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      Type intersect_type = T.Intersect(type, T.Any);
      CheckEqual(intersect_type, type);
    }

    // Domination: Intersect(T, None) = None
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      Type intersect_type = T.Intersect(type, T.None);
      CheckEqual(intersect_type, T.None);
    }

    // Idempotence: Intersect(T, T) = T
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      Type type = *it;
      Type intersect_type = T.Intersect(type, type);
      CheckEqual(intersect_type, type);
    }

    // Commutativity: Intersect(T1, T2) = Intersect(T2, T1)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type intersect12 = T.Intersect(type1, type2);
        Type intersect21 = T.Intersect(type2, type1);
        CheckEqual(intersect12, intersect21);
      }
    }

    // Lower Boundedness: T1.Is(T2) implies Intersect(T1, T2) = T1
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        Type type1 = *it1;
        Type type2 = *it2;
        Type intersect12 = T.Intersect(type1, type2);
        if (type1.Is(type2)) CheckEqual(intersect12, type1);
      }
    }

    // Monotonicity: T1.Is(T2) and T1.Is(T3) implies T1.Is(Intersect(T2, T3))
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      HandleScope scope(isolate());
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          Type type1 = *it1;
          Type type2 = *it2;
          Type type3 = *it3;
          Type intersect23 = T.Intersect(type2, type3);
          CHECK(!(type1.Is(type2) && type1.Is(type3)) || type1.Is(intersect23));
        }
      }
    }

    // Constant-union
    CheckEqual(T.Intersect(T.ObjectConstant1,
                           T.Union(T.ObjectConstant1, T.ObjectConstant2)),
               T.ObjectConstant1);
    CheckEqual(T.Intersect(T.SmiConstant, T.Union(T.Number, T.ObjectConstant2)),
               T.SmiConstant);

    // Union-union
    CheckEqual(T.Intersect(T.Union(T.ObjectConstant2, T.ObjectConstant1),
                           T.Union(T.ObjectConstant1, T.ObjectConstant2)),
               T.Union(T.ObjectConstant2, T.ObjectConstant1));
  }

  void Distributivity() {
    // Union(T1, Intersect(T2, T3)) = Intersect(Union(T1, T2), Union(T1, T3))
    // This does NOT hold.  For example:
    // Untagged \/ (Untagged /\ Class(../Tagged)) = Untagged \/ Class(../Tagged)
    // (Untagged \/ Untagged) /\ (Untagged \/ Class(../Tagged)) =
    // Untagged /\ (Untagged \/ Class(../Tagged)) = Untagged
    // because Untagged <= Untagged \/ Class(../Tagged)
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          Type type1 = *it1;
          Type type2 = *it2;
          Type type3 = *it3;
          Type union12 = T.Union(type1, type2);
          Type union13 = T.Union(type1, type3);
          Type intersect23 = T.Intersect(type2, type3);
          Type union1_23 = T.Union(type1, intersect23);
          Type intersect12_13 = T.Intersect(union12, union13);
          CHECK(Equal(union1_23, intersect12_13));
        }
      }
    }
    */

    // Intersect(T1, Union(T2, T3)) = Union(Intersect(T1, T2), Intersect(T1,T3))
    // This does NOT hold.  For example:
    // Untagged /\ (Untagged \/ Class(../Tagged)) = Untagged
    // (Untagged /\ Untagged) \/ (Untagged /\ Class(../Tagged)) =
    // Untagged \/ Class(../Tagged)
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          Type type1 = *it1;
          Type type2 = *it2;
          Type type3 = *it3;
          Type intersect12 = T.Intersect(type1, type2);
          Type intersect13 = T.Intersect(type1, type3);
          Type union23 = T.Union(type2, type3);
          Type intersect1_23 = T.Intersect(type1, union23);
          Type union12_13 = T.Union(intersect12, intersect13);
          CHECK(Equal(intersect1_23, union12_13));
        }
      }
    }
    */
  }

  void GetRange() {
    // GetRange(Range(a, b)) = Range(a, b).
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      Type type1 = *it1;
      if (type1.IsRange()) {
        const RangeType* range = type1.GetRange().AsRange();
        CHECK(type1.Min() == range->Min());
        CHECK(type1.Max() == range->Max());
      }
    }
  }
};

}  // namespace

TEST_F(TypesTest, IsSomeType) { IsSomeType(); }
TEST_F(TypesTest, BitsetType) { Bitset(); }
TEST_F(TypesTest, ConstantType) { Constant(); }
TEST_F(TypesTest, RangeType) { Range(); }
TEST_F(TypesTest, MinMax) { MinMax(); }
TEST_F(TypesTest, BitsetGlb) { BitsetGlb(); }
TEST_F(TypesTest, BitsetLub) { BitsetLub(); }
TEST_F(TypesTest, Is1) { Is1(); }
TEST_F(TypesTest, Is2) { Is2(); }
TEST_F(TypesTest, Maybe) { Maybe(); }
TEST_F(TypesTest, Union1) { Union1(); }
TEST_F(TypesTest, Union2) { Union2(); }
TEST_F(TypesTest, Union3) { Union3(); }
TEST_F(TypesTest, Union4) { Union4(); }
TEST_F(TypesTest, Intersect) { Intersect(); }
TEST_F(TypesTest, Distributivity) { Distributivity(); }
TEST_F(TypesTest, GetRange) { GetRange(); }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
