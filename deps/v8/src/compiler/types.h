// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TYPES_H_
#define V8_COMPILER_TYPES_H_

#include "src/base/compiler-specific.h"
#include "src/common/globals.h"
#include "src/compiler/heap-refs.h"
#include "src/handles/handles.h"
#include "src/numbers/conversions.h"
#include "src/objects/objects.h"
#include "src/utils/ostreams.h"

#ifdef V8_ENABLE_WEBASSEMBLY
#include "src/wasm/value-type.h"
#endif

namespace v8 {
namespace internal {
namespace wasm {
struct TypeInModule;
}
namespace compiler {

// SUMMARY
//
// A simple type system for compiler-internal use. It is based entirely on
// union types, and all subtyping hence amounts to set inclusion. Besides the
// obvious primitive types and some predefined unions, the type language also
// can express class types (a.k.a. specific maps) and singleton types (i.e.,
// concrete constants).
//
// The following equations and inequations hold:
//
//   None <= T
//   T <= Any
//
//   Number = Signed32 \/ Unsigned32 \/ Double
//   Smi <= Signed32
//   Name = String \/ Symbol
//   UniqueName = InternalizedString \/ Symbol
//   InternalizedString < String
//
//   Receiver = Object \/ Proxy
//   OtherUndetectable < Object
//   DetectableReceiver = Receiver - OtherUndetectable
//
//   Constant(x) < T  iff instance_type(map(x)) < T
//
//    None <= Machine <= Any
//
// RANGE TYPES
//
// A range type represents a continuous integer interval by its minimum and
// maximum value.  Either value may be an infinity, in which case that infinity
// itself is also included in the range.   A range never contains NaN or -0.
//
// If a value v happens to be an integer n, then Constant(v) is considered a
// subtype of Range(n, n) (and therefore also a subtype of any larger range).
// In order to avoid large unions, however, it is usually a good idea to use
// Range rather than Constant.
//
//
// PREDICATES
//
// There are two main functions for testing types:
//
//   T1.Is(T2)     -- tests whether T1 is included in T2 (i.e., T1 <= T2)
//   T1.Maybe(T2)  -- tests whether T1 and T2 overlap (i.e., T1 /\ T2 =/= 0)
//
// Typically, the former is to be used to select representations (e.g., via
// T.Is(SignedSmall())), and the latter to check whether a specific case needs
// handling (e.g., via T.Maybe(Number())).
//
// There is no functionality to discover whether a type is a leaf in the
// lattice. That is intentional. It should always be possible to refine the
// lattice (e.g., splitting up number types further) without invalidating any
// existing assumptions or tests.
// Consequently, do not normally use Equals for type tests, always use Is!
//
//
// PROPERTIES
//
// Various formal properties hold for constructors, operators, and predicates
// over types. For example, constructors are injective and subtyping is a
// complete partial order.
//
// See test/cctest/test-types.cc for a comprehensive executable specification,
// especially with respect to the properties of the more exotic 'temporal'
// constructors and predicates (those prefixed 'Now').
//
//
// IMPLEMENTATION
//
// Internally, all 'primitive' types, and their unions, are represented as
// bitsets. Bit 0 is reserved for tagging. Only structured types require
// allocation.

// -----------------------------------------------------------------------------
// Values for bitset types

// clang-format off

#define INTERNAL_BITSET_TYPE_LIST(V)    \
  V(OtherUnsigned31, uint64_t{1} << 1)  \
  V(OtherUnsigned32, uint64_t{1} << 2)  \
  V(OtherSigned32,   uint64_t{1} << 3)  \
  V(OtherNumber,     uint64_t{1} << 4)  \
  V(OtherString,     uint64_t{1} << 5)  \

#define PROPER_ATOMIC_BITSET_TYPE_LOW_LIST(V) \
  V(Negative31,               uint64_t{1} << 6)   \
  V(Null,                     uint64_t{1} << 7)   \
  V(Undefined,                uint64_t{1} << 8)   \
  V(Boolean,                  uint64_t{1} << 9)   \
  V(Unsigned30,               uint64_t{1} << 10)  \
  V(MinusZero,                uint64_t{1} << 11)  \
  V(NaN,                      uint64_t{1} << 12)  \
  V(Symbol,                   uint64_t{1} << 13)  \
  V(InternalizedString,       uint64_t{1} << 14)  \
  V(OtherCallable,            uint64_t{1} << 15)  \
  V(OtherObject,              uint64_t{1} << 16)  \
  V(OtherUndetectable,        uint64_t{1} << 17)  \
  V(CallableProxy,            uint64_t{1} << 18)  \
  V(OtherProxy,               uint64_t{1} << 19)  \
  V(CallableFunction,         uint64_t{1} << 20)  \
  V(ClassConstructor,         uint64_t{1} << 21)  \
  V(BoundFunction,            uint64_t{1} << 22)  \
  V(OtherInternal,            uint64_t{1} << 23)  \
  V(ExternalPointer,          uint64_t{1} << 24)  \
  V(Array,                    uint64_t{1} << 25)  \
  V(UnsignedBigInt63,         uint64_t{1} << 26)  \
  V(OtherUnsignedBigInt64,    uint64_t{1} << 27)  \
  V(NegativeBigInt63,         uint64_t{1} << 28)  \
  V(OtherBigInt,              uint64_t{1} << 29)  \
  V(WasmObject,               uint64_t{1} << 30)  \
  V(SandboxedPointer,         uint64_t{1} << 31)

// We split the macro list into two parts because the Torque equivalent in
// turbofan-types.tq uses two 32bit bitfield structs.
#define PROPER_ATOMIC_BITSET_TYPE_HIGH_LIST(V)                             \
  V(Machine,                  uint64_t{1} << 32)                           \
  V(Hole,                     uint64_t{1} << 33) \
  V(StringWrapper,            uint64_t{1} << 34)

#define PROPER_BITSET_TYPE_LIST(V) \
  V(None,                     uint64_t{0}) \
  PROPER_ATOMIC_BITSET_TYPE_LOW_LIST(V) \
  PROPER_ATOMIC_BITSET_TYPE_HIGH_LIST(V) \
  V(Signed31,                     kUnsigned30 | kNegative31) \
  V(Signed32,                     kSigned31 | kOtherUnsigned31 | \
                                  kOtherSigned32) \
  V(Signed32OrMinusZero,          kSigned32 | kMinusZero) \
  V(Signed32OrMinusZeroOrNaN,     kSigned32 | kMinusZero | kNaN) \
  V(Negative32,                   kNegative31 | kOtherSigned32) \
  V(Unsigned31,                   kUnsigned30 | kOtherUnsigned31) \
  V(Unsigned32,                   kUnsigned30 | kOtherUnsigned31 | \
                                  kOtherUnsigned32) \
  V(Unsigned32OrMinusZero,        kUnsigned32 | kMinusZero) \
  V(Unsigned32OrMinusZeroOrNaN,   kUnsigned32 | kMinusZero | kNaN) \
  V(Integral32,                   kSigned32 | kUnsigned32) \
  V(Integral32OrMinusZero,        kIntegral32 | kMinusZero) \
  V(Integral32OrMinusZeroOrNaN,   kIntegral32OrMinusZero | kNaN) \
  V(PlainNumber,                  kIntegral32 | kOtherNumber) \
  V(OrderedNumber,                kPlainNumber | kMinusZero) \
  V(MinusZeroOrNaN,               kMinusZero | kNaN) \
  V(Number,                       kOrderedNumber | kNaN) \
  V(SignedBigInt64,               kUnsignedBigInt63 | kNegativeBigInt63) \
  V(UnsignedBigInt64,             kUnsignedBigInt63 | kOtherUnsignedBigInt64) \
  V(BigInt,                       kSignedBigInt64 | kOtherUnsignedBigInt64 | \
                                  kOtherBigInt) \
  V(Numeric,                      kNumber | kBigInt) \
  V(String,                       kInternalizedString | kOtherString) \
  V(StringOrStringWrapper,        kString | kStringWrapper) \
  V(UniqueName,                   kSymbol | kInternalizedString) \
  V(Name,                         kSymbol | kString) \
  V(InternalizedStringOrNull,     kInternalizedString | kNull) \
  V(BooleanOrNumber,              kBoolean | kNumber) \
  V(BooleanOrNullOrNumber,        kBooleanOrNumber | kNull) \
  V(BooleanOrNullOrUndefined,     kBoolean | kNull | kUndefined) \
  V(NullOrNumber,                 kNull | kNumber) \
  V(NullOrUndefined,              kNull | kUndefined) \
  V(Undetectable,                 kNullOrUndefined | kOtherUndetectable) \
  V(NumberOrHole,                 kNumber | kHole) \
  V(NumberOrOddball,              kNumber | kBooleanOrNullOrUndefined ) \
  V(NumberOrOddballOrHole,        kNumberOrOddball| kHole ) \
  V(NumericOrString,              kNumeric | kString) \
  V(NumberOrUndefined,            kNumber | kUndefined) \
  V(PlainPrimitive,               kNumber | kString | kBoolean | \
                                  kNullOrUndefined) \
  V(NonBigIntPrimitive,           kSymbol | kPlainPrimitive) \
  V(Primitive,                    kBigInt | kNonBigIntPrimitive) \
  V(OtherUndetectableOrUndefined, kOtherUndetectable | kUndefined) \
  V(Proxy,                        kCallableProxy | kOtherProxy) \
  V(ArrayOrOtherObject,           kArray | kOtherObject) \
  V(ArrayOrProxy,                 kArray | kProxy) \
  V(StringWrapperOrOtherObject,   kStringWrapper | kOtherObject) \
  V(Function,                     kCallableFunction | kClassConstructor) \
  V(DetectableCallable,           kFunction | kBoundFunction | \
                                  kOtherCallable | kCallableProxy) \
  V(Callable,                     kDetectableCallable | kOtherUndetectable) \
  V(NonCallable,                  kArray | kStringWrapper | kOtherObject | \
                                  kOtherProxy) \
  V(NonCallableOrNull,            kNonCallable | kNull) \
  V(DetectableObject,             kArray | kFunction | kBoundFunction | \
                                  kStringWrapper | kOtherCallable | \
                                  kOtherObject) \
  V(DetectableReceiver,           kDetectableObject | kProxy | kWasmObject) \
  V(DetectableReceiverOrNull,     kDetectableReceiver | kNull) \
  V(Object,                       kDetectableObject | kOtherUndetectable) \
  V(Receiver,                     kObject | kProxy | kWasmObject) \
  V(ReceiverOrUndefined,          kReceiver | kUndefined) \
  V(ReceiverOrNull,               kReceiver | kNull) \
  V(ReceiverOrNullOrUndefined,    kReceiver | kNull | kUndefined) \
  V(SymbolOrReceiver,             kSymbol | kReceiver) \
  V(StringOrReceiver,             kString | kReceiver) \
  V(Unique,                       kBoolean | kUniqueName | kNull | \
                                  kUndefined | kHole | kReceiver) \
  V(Internal,                     kHole | kExternalPointer | \
                                  kSandboxedPointer | kOtherInternal) \
  V(NonInternal,                  kPrimitive | kReceiver) \
  V(NonBigInt,                    kNonBigIntPrimitive | kReceiver) \
  V(NonNumber,                    kBigInt | kUnique | kString | kInternal) \
  V(Any,                          uint64_t{0xfffffffffffffffe})

// clang-format on

/*
 * The following diagrams show how integers (in the mathematical sense) are
 * divided among the different atomic numerical types.
 *
 *   ON    OS32     N31     U30     OU31    OU32     ON
 * ______[_______[_______[_______[_______[_______[_______
 *     -2^31   -2^30     0      2^30    2^31    2^32
 *
 * E.g., OtherUnsigned32 (OU32) covers all integers from 2^31 to 2^32-1.
 *
 * Some of the atomic numerical bitsets are internal only (see
 * INTERNAL_BITSET_TYPE_LIST).  To a types user, they should only occur in
 * union with certain other bitsets.  For instance, OtherNumber should only
 * occur as part of PlainNumber.
 */

#define BITSET_TYPE_LIST(V)    \
  INTERNAL_BITSET_TYPE_LIST(V) \
  PROPER_BITSET_TYPE_LIST(V)

class JSHeapBroker;
class HeapConstantType;
class OtherNumberConstantType;
class TupleType;
class Type;
class UnionType;

// -----------------------------------------------------------------------------
// Bitset types (internal).

class V8_EXPORT_PRIVATE BitsetType {
 public:
  using bitset = uint64_t;  // Internal

  enum : bitset {
#define DECLARE_TYPE(type, value) k##type = (value),
    BITSET_TYPE_LIST(DECLARE_TYPE)
#undef DECLARE_TYPE
        kUnusedEOL = 0
  };

  static bitset SignedSmall();
  static bitset UnsignedSmall();

  static bool IsNone(bitset bits) { return bits == kNone; }

  static bool Is(bitset bits1, bitset bits2) {
    return (bits1 | bits2) == bits2;
  }

  static double Min(bitset);
  static double Max(bitset);

  static bitset Glb(double min, double max);
  static bitset Lub(HeapObjectType const& type, JSHeapBroker* broker) {
    return Lub<HeapObjectType>(type, broker);
  }
  static bitset Lub(MapRef map, JSHeapBroker* broker) {
    return Lub<MapRef>(map, broker);
  }
  static bitset Lub(double value);
  static bitset Lub(double min, double max);
  static bitset ExpandInternals(bitset bits);

  static const char* Name(bitset);
  static void Print(std::ostream& os, bitset);
#ifdef DEBUG
  static void Print(bitset);
#endif

  static bitset NumberBits(bitset bits);

 private:
  struct Boundary {
    bitset internal;
    bitset external;
    double min;
  };
  static const Boundary BoundariesArray[];
  static inline const Boundary* Boundaries();
  static inline size_t BoundariesSize();

  template <typename MapRefLike>
  static bitset Lub(MapRefLike map, JSHeapBroker* broker);
};

// -----------------------------------------------------------------------------
// Superclass for non-bitset types (internal).
class TypeBase {
 protected:
  friend class Type;

  enum Kind {
    kHeapConstant,
    kOtherNumberConstant,
    kTuple,
    kUnion,
    kRange,
    kWasm
  };

  Kind kind() const { return kind_; }
  explicit TypeBase(Kind kind) : kind_(kind) {}

  static bool IsKind(Type type, Kind kind);

 private:
  Kind kind_;
};

// -----------------------------------------------------------------------------
// Range types.

class RangeType : public TypeBase {
 public:
  struct Limits {
    double min;
    double max;
    Limits(double min, double max) : min(min), max(max) {}
    explicit Limits(const RangeType* range)
        : min(range->Min()), max(range->Max()) {}
    bool IsEmpty();
    static Limits Empty() { return Limits(1, 0); }
    static Limits Intersect(Limits lhs, Limits rhs);
    static Limits Union(Limits lhs, Limits rhs);
  };

  double Min() const { return limits_.min; }
  double Max() const { return limits_.max; }

  static bool IsInteger(double x) {
    return nearbyint(x) == x && !IsMinusZero(x);  // Allows for infinities.
  }

 private:
  friend class Type;
  friend class BitsetType;
  friend class UnionType;
  friend Zone;

  static RangeType* New(double min, double max, Zone* zone) {
    return New(Limits(min, max), zone);
  }

  static RangeType* New(Limits lim, Zone* zone) {
    DCHECK(IsInteger(lim.min) && IsInteger(lim.max));
    DCHECK(lim.min <= lim.max);
    BitsetType::bitset bits = BitsetType::Lub(lim.min, lim.max);

    return zone->New<RangeType>(bits, lim);
  }

  RangeType(BitsetType::bitset bitset, Limits limits)
      : TypeBase(kRange), bitset_(bitset), limits_(limits) {}

  BitsetType::bitset Lub() const { return bitset_; }

  BitsetType::bitset bitset_;
  Limits limits_;
};

#ifdef V8_ENABLE_WEBASSEMBLY
class WasmType : public TypeBase {
 public:
  static WasmType* New(wasm::ValueType value_type,
                       const wasm::WasmModule* module, Zone* zone) {
    return zone->New<WasmType>(value_type, module);
  }
  wasm::ValueType value_type() const { return value_type_; }
  const wasm::WasmModule* module() const { return module_; }

 private:
  friend class Type;
  friend Zone;

  explicit WasmType(wasm::ValueType value_type, const wasm::WasmModule* module)
      : TypeBase(kWasm), value_type_(value_type), module_(module) {}

  BitsetType::bitset Lub() const {
    // TODO(manoskouk): Specify more concrete types.
    return BitsetType::kAny;
  }

  wasm::ValueType value_type_;
  const wasm::WasmModule* module_;
};
#endif  // V8_ENABLE_WEBASSEMBLY

// -----------------------------------------------------------------------------
// The actual type.

class V8_EXPORT_PRIVATE Type {
 public:
  using bitset = BitsetType::bitset;  // Internal

// Constructors.
#define DEFINE_TYPE_CONSTRUCTOR(type, value) \
  static Type type() { return NewBitset(BitsetType::k##type); }
  PROPER_BITSET_TYPE_LIST(DEFINE_TYPE_CONSTRUCTOR)
#undef DEFINE_TYPE_CONSTRUCTOR

  Type() : payload_(uint64_t{0}) {}

  static Type SignedSmall() { return NewBitset(BitsetType::SignedSmall()); }
  static Type UnsignedSmall() { return NewBitset(BitsetType::UnsignedSmall()); }

  static Type Constant(JSHeapBroker* broker, Handle<i::Object> value,
                       Zone* zone);
  static Type Constant(JSHeapBroker* broker, ObjectRef value, Zone* zone);
  static Type Constant(double value, Zone* zone);
  static Type Range(double min, double max, Zone* zone);
  static Type Tuple(Type first, Type second, Type third, Zone* zone);
  static Type Tuple(Type first, Type second, Zone* zone);

  static Type Union(Type type1, Type type2, Zone* zone);
  static Type Intersect(Type type1, Type type2, Zone* zone);
#ifdef V8_ENABLE_WEBASSEMBLY
  static Type Wasm(wasm::ValueType value_type, const wasm::WasmModule* module,
                   Zone* zone);
  static Type Wasm(wasm::TypeInModule type_in_module, Zone* zone);
#endif

  static Type For(MapRef type, JSHeapBroker* broker) {
    return NewBitset(
        BitsetType::ExpandInternals(BitsetType::Lub(type, broker)));
  }

  // Predicates.
  bool IsNone() const { return payload_ == None().payload_; }
  bool IsInvalid() const { return payload_ == uint64_t{0}; }

  bool Is(Type that) const {
    return payload_ == that.payload_ || this->SlowIs(that);
  }
  bool Maybe(Type that) const;
  bool Equals(Type that) const { return this->Is(that) && that.Is(*this); }

  // Inspection.
  bool IsBitset() const { return payload_ & uint64_t{1}; }
  bool IsRange() const { return IsKind(TypeBase::kRange); }
  bool IsHeapConstant() const { return IsKind(TypeBase::kHeapConstant); }
  bool IsOtherNumberConstant() const {
    return IsKind(TypeBase::kOtherNumberConstant);
  }
  bool IsTuple() const { return IsKind(TypeBase::kTuple); }
#ifdef V8_ENABLE_WEBASSEMBLY
  bool IsWasm() const { return IsKind(TypeBase::kWasm); }
#endif

  bool IsSingleton() const {
    if (IsNone()) return false;
    return Is(Type::Null()) || Is(Type::Undefined()) || Is(Type::MinusZero()) ||
           Is(Type::NaN()) || IsHeapConstant() ||
           (Is(Type::PlainNumber()) && Min() == Max());
  }

  bool CanBeAsserted() const { return Is(Type::NonInternal()); }
  Handle<TurbofanType> AllocateOnHeap(Factory* factory);

  const HeapConstantType* AsHeapConstant() const;
  const OtherNumberConstantType* AsOtherNumberConstant() const;
  const RangeType* AsRange() const;
  const TupleType* AsTuple() const;
#ifdef V8_ENABLE_WEBASSEMBLY
  wasm::TypeInModule AsWasm() const;
#endif

  // Minimum and maximum of a numeric type.
  // These functions do not distinguish between -0 and +0.  NaN is ignored.
  // Only call them on subtypes of Number whose intersection with OrderedNumber
  // is not empty.
  double Min() const;
  double Max() const;

  // Extracts a range from the type: if the type is a range or a union
  // containing a range, that range is returned; otherwise, nullptr is returned.
  Type GetRange() const;

  int NumConstants() const;

  static Type Invalid() { return Type(); }

  bool operator==(Type other) const { return payload_ == other.payload_; }
  bool operator!=(Type other) const { return payload_ != other.payload_; }

  // Printing.

  void PrintTo(std::ostream& os) const;

#ifdef DEBUG
  void Print() const;
#endif

  // Helpers for testing.
  bool IsUnionForTesting() { return IsUnion(); }
  bitset AsBitsetForTesting() { return AsBitset(); }
  const UnionType* AsUnionForTesting() { return AsUnion(); }
  Type BitsetGlbForTesting() { return NewBitset(BitsetGlb()); }
  Type BitsetLubForTesting() { return NewBitset(BitsetLub()); }

 private:
  // Friends.
  template <class>
  friend class Iterator;
  friend BitsetType;
  friend UnionType;
  friend size_t hash_value(Type type);

  explicit Type(bitset bits) : payload_(bits | uint64_t{1}) {}

  Type(TypeBase* type_base)  // NOLINT(runtime/explicit)
      : payload_(reinterpret_cast<uint64_t>(type_base)) {}

  // Internal inspection.
  bool IsKind(TypeBase::Kind kind) const {
    if (IsBitset()) return false;
    const TypeBase* base = ToTypeBase();
    return base->kind() == kind;
  }

  const TypeBase* ToTypeBase() const {
    return reinterpret_cast<TypeBase*>(payload_);
  }
  static Type FromTypeBase(TypeBase* type) { return Type(type); }

  bool IsAny() const { return payload_ == Any().payload_; }
  bool IsUnion() const { return IsKind(TypeBase::kUnion); }

  bitset AsBitset() const {
    DCHECK(IsBitset());
    return static_cast<bitset>(payload_) ^ uint64_t { 1 };
  }

  const UnionType* AsUnion() const;

  bitset BitsetGlb() const;  // greatest lower bound that's a bitset
  bitset BitsetLub() const;  // least upper bound that's a bitset

  bool SlowIs(Type that) const;

  static Type NewBitset(bitset bits) { return Type(bits); }

  static Type Range(RangeType::Limits lims, Zone* zone);
  static Type OtherNumberConstant(double value, Zone* zone);
  static Type HeapConstant(HeapObjectRef value, JSHeapBroker* broker,
                           Zone* zone);

  static bool Overlap(const RangeType* lhs, const RangeType* rhs);
  static bool Contains(const RangeType* lhs, const RangeType* rhs);

  static int UpdateRange(Type type, UnionType* result, int size, Zone* zone);

  static RangeType::Limits IntersectRangeAndBitset(Type range, Type bits,
                                                   Zone* zone);
  static RangeType::Limits ToLimits(bitset bits, Zone* zone);

  bool SimplyEquals(Type that) const;

  static int AddToUnion(Type type, UnionType* result, int size, Zone* zone);
  static int IntersectAux(Type type, Type other, UnionType* result, int size,
                          RangeType::Limits* limits, Zone* zone);
  static Type NormalizeUnion(UnionType* unioned, int size, Zone* zone);
  static Type NormalizeRangeAndBitset(Type range, bitset* bits, Zone* zone);

  // If LSB is set, the payload is a bitset; if LSB is clear, the payload is
  // a pointer to a subtype of the TypeBase class.
  uint64_t payload_;
};

inline size_t hash_value(Type type) { return type.payload_; }
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os, Type type);

// -----------------------------------------------------------------------------
// Constant types.

class OtherNumberConstantType : public TypeBase {
 public:
  double Value() const { return value_; }

  static bool IsOtherNumberConstant(double value);

 private:
  friend class Type;
  friend class BitsetType;
  friend Zone;

  static OtherNumberConstantType* New(double value, Zone* zone) {
    return zone->New<OtherNumberConstantType>(value);
  }

  explicit OtherNumberConstantType(double value)
      : TypeBase(kOtherNumberConstant), value_(value) {
    CHECK(IsOtherNumberConstant(value));
  }

  BitsetType::bitset Lub() const { return BitsetType::kOtherNumber; }

  double value_;
};

class V8_EXPORT_PRIVATE HeapConstantType : public NON_EXPORTED_BASE(TypeBase) {
 public:
  Handle<HeapObject> Value() const;
  HeapObjectRef Ref() const { return heap_ref_; }

 private:
  friend class Type;
  friend class BitsetType;
  friend Zone;

  static HeapConstantType* New(HeapObjectRef heap_ref,
                               BitsetType::bitset bitset, Zone* zone) {
    return zone->New<HeapConstantType>(bitset, heap_ref);
  }

  HeapConstantType(BitsetType::bitset bitset, HeapObjectRef heap_ref);

  BitsetType::bitset Lub() const { return bitset_; }

  BitsetType::bitset bitset_;
  HeapObjectRef heap_ref_;
};

// -----------------------------------------------------------------------------
// Superclass for types with variable number of type fields.
class StructuralType : public TypeBase {
 public:
  int LengthForTesting() const { return Length(); }

 protected:
  friend class Type;

  int Length() const { return length_; }

  Type Get(int i) const {
    DCHECK(0 <= i && i < this->Length());
    return elements_[i];
  }

  void Set(int i, Type type) {
    DCHECK(0 <= i && i < this->Length());
    elements_[i] = type;
  }

  void Shrink(int length) {
    DCHECK(2 <= length && length <= this->Length());
    length_ = length;
  }

  StructuralType(Kind kind, int length, Zone* zone)
      : TypeBase(kind), length_(length) {
    elements_ = zone->AllocateArray<Type>(length);
  }

 private:
  int length_;
  Type* elements_;
};

// -----------------------------------------------------------------------------
// Tuple types.

class TupleType : public StructuralType {
 public:
  int Arity() const { return this->Length(); }
  Type Element(int i) const { return this->Get(i); }

  void InitElement(int i, Type type) { this->Set(i, type); }

 private:
  friend Type;
  friend Zone;

  TupleType(int length, Zone* zone) : StructuralType(kTuple, length, zone) {}

  static TupleType* New(int length, Zone* zone) {
    return zone->New<TupleType>(length, zone);
  }
};

// -----------------------------------------------------------------------------
// Union types (internal).
// A union is a structured type with the following invariants:
// - its length is at least 2
// - at most one field is a bitset, and it must go into index 0
// - no field is a union
// - no field is a subtype of any other field
class UnionType : public StructuralType {
 private:
  friend Type;
  friend BitsetType;
  friend Zone;

  UnionType(int length, Zone* zone) : StructuralType(kUnion, length, zone) {}

  static UnionType* New(int length, Zone* zone) {
    return zone->New<UnionType>(length, zone);
  }

  bool Wellformed() const;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TYPES_H_
