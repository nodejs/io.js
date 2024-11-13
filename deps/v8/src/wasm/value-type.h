// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_VALUE_TYPE_H_
#define V8_WASM_VALUE_TYPE_H_

#include <optional>

#include "src/base/bit-field.h"
#include "src/codegen/machine-type.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-limits.h"

namespace v8 {
namespace internal {

template <typename T>
class Signature;

// Type for holding simd values, defined in simd128.h.
class Simd128;
class Zone;

namespace wasm {

// Format: kind, log2Size, code, machineType, shortName, typeName
//
// Some of these types are from proposals that are not standardized yet:
// - "ref"/"ref null" https://github.com/WebAssembly/function-references
// - "rtt", "i8" and "i16" per https://github.com/WebAssembly/gc
#define FOREACH_NUMERIC_VALUE_TYPE(V)    \
  V(I32, 2, I32, Int32, 'i', "i32")      \
  V(I64, 3, I64, Int64, 'l', "i64")      \
  V(F32, 2, F32, Float32, 'f', "f32")    \
  V(F64, 3, F64, Float64, 'd', "f64")    \
  V(S128, 4, S128, Simd128, 's', "v128") \
  V(I8, 0, I8, Int8, 'b', "i8")          \
  V(I16, 1, I16, Int16, 'h', "i16")      \
  V(F16, 1, F16, Float16, 'p', "f16")

#define FOREACH_VALUE_TYPE(V)                                      \
  V(Void, -1, Void, None, 'v', "<void>")                           \
  FOREACH_NUMERIC_VALUE_TYPE(V)                                    \
  V(Rtt, kTaggedSizeLog2, Rtt, TaggedPointer, 't', "rtt")          \
  V(Ref, kTaggedSizeLog2, Ref, AnyTagged, 'r', "ref")              \
  V(RefNull, kTaggedSizeLog2, RefNull, AnyTagged, 'n', "ref null") \
  V(Bottom, -1, Void, None, '*', "<bot>")

constexpr int kMaxValueTypeSize = 16;  // bytes

// Represents a WebAssembly heap type, as per the typed-funcref and gc
// proposals.
// The underlying Representation enumeration encodes heap types as follows:
// a number t < kV8MaxWasmTypes represents the type defined in the module at
// index t. Numbers directly beyond that represent the generic heap types. The
// next number represents the bottom heap type (internal use).
class HeapType {
 public:
  enum Representation : uint32_t {
    kFunc = kV8MaxWasmTypes,  // shorthand: c
    kEq,                      // shorthand: q
    kI31,                     // shorthand: j
    kStruct,                  // shorthand: o
    kArray,                   // shorthand: g
    kAny,                     //
    kExtern,                  // shorthand: a.
    kExternString,            // Internal type for optimization purposes.
                              // Subtype of extern.
                              // Used by the js-builtin-strings proposal.
    kExn,                     //
    kString,                  // shorthand: w.
    kStringViewWtf8,          // shorthand: x.
    kStringViewWtf16,         // shorthand: y.
    kStringViewIter,          // shorthand: z.
    kNone,                    //
    kNoFunc,                  //
    kNoExtern,                //
    kNoExn,                   //
    kFuncShared,
    kEqShared,
    kI31Shared,
    kStructShared,
    kArrayShared,
    kAnyShared,
    kExternShared,
    kExternStringShared,
    kExnShared,
    kStringShared,
    kStringViewWtf8Shared,
    kStringViewWtf16Shared,
    kStringViewIterShared,
    kNoneShared,
    kNoFuncShared,
    kNoExternShared,
    kNoExnShared,
    // This value is used to represent failures in the parsing of heap types and
    // does not correspond to a wasm heap type. It has to be last in this list.
    kBottom
  };

  static constexpr HeapType from_code(uint8_t code, bool is_shared) {
    switch (code) {
      case ValueTypeCode::kFuncRefCode:
        return HeapType(is_shared ? kFuncShared : kFunc);
      case ValueTypeCode::kEqRefCode:
        return HeapType(is_shared ? kEqShared : kEq);
      case ValueTypeCode::kI31RefCode:
        return HeapType(is_shared ? kI31Shared : kI31);
      case ValueTypeCode::kAnyRefCode:
        return HeapType(is_shared ? kAnyShared : kAny);
      case ValueTypeCode::kExternRefCode:
        return HeapType(is_shared ? kExternShared : kExtern);
      case ValueTypeCode::kExnRefCode:
        return HeapType(is_shared ? kExnShared : kExn);
      case ValueTypeCode::kStructRefCode:
        return HeapType(is_shared ? kStructShared : kStruct);
      case ValueTypeCode::kArrayRefCode:
        return HeapType(is_shared ? kArrayShared : kArray);
      case ValueTypeCode::kStringRefCode:
        return HeapType(is_shared ? kStringShared : kString);
      case ValueTypeCode::kStringViewWtf8Code:
        return HeapType(is_shared ? kStringViewWtf8Shared : kStringViewWtf8);
      case ValueTypeCode::kStringViewWtf16Code:
        return HeapType(is_shared ? kStringViewWtf16Shared : kStringViewWtf16);
      case ValueTypeCode::kStringViewIterCode:
        return HeapType(is_shared ? kStringViewIterShared : kStringViewIter);
      case ValueTypeCode::kNoneCode:
        return HeapType(is_shared ? kNoneShared : kNone);
      case ValueTypeCode::kNoExternCode:
        return HeapType(is_shared ? kNoExternShared : kNoExtern);
      case ValueTypeCode::kNoFuncCode:
        return HeapType(is_shared ? kNoFuncShared : kNoFunc);
      case ValueTypeCode::kNoExnCode:
        return HeapType(is_shared ? kNoExnShared : kNoExn);
      default:
        return HeapType(kBottom);
    }
  }

  explicit constexpr HeapType(Representation repr) : representation_(repr) {
    DCHECK(is_bottom() || is_valid());
  }
  explicit constexpr HeapType(uint32_t repr)
      : HeapType(static_cast<Representation>(repr)) {}

  constexpr bool operator==(HeapType other) const {
    return representation_ == other.representation_;
  }
  constexpr bool operator!=(HeapType other) const {
    return representation_ != other.representation_;
  }

  constexpr bool operator==(Representation other) const {
    return representation_ == other;
  }

  constexpr bool operator!=(Representation other) const {
    return representation_ != other;
  }

  constexpr Representation representation() const { return representation_; }
  constexpr uint32_t ref_index() const {
    DCHECK(is_index());
    return representation_;
  }

  constexpr bool is_abstract() const {
    return !is_bottom() && representation_ >= kFirstSentinel;
  }

  constexpr bool is_index() const { return representation_ < kFirstSentinel; }

  constexpr bool is_bottom() const { return representation_ == kBottom; }

  constexpr bool is_string_view() const {
    return representation_ == kStringViewWtf8 ||
           representation_ == kStringViewWtf16 ||
           representation_ == kStringViewIter;
  }

  std::string name() const {
    switch (representation_) {
      case kFunc:
        return std::string("func");
      case kEq:
        return std::string("eq");
      case kI31:
        return std::string("i31");
      case kStruct:
        return std::string("struct");
      case kArray:
        return std::string("array");
      case kExtern:
        return std::string("extern");
      case kExternString:
        return std::string("<extern_string>");
      case kAny:
        return std::string("any");
      case kString:
        return std::string("string");
      case kStringViewWtf8:
        return std::string("stringview_wtf8");
      case kStringViewWtf16:
        return std::string("stringview_wtf16");
      case kStringViewIter:
        return std::string("stringview_iter");
      case kNone:
        return std::string("none");
      case kNoExtern:
        return std::string("noextern");
      case kNoFunc:
        return std::string("nofunc");
      case kNoExn:
        return std::string("noexn");
      case kExn:
        return std::string("exn");
      case kFuncShared:
        return std::string("shared func");
      case kEqShared:
        return std::string("shared eq");
      case kI31Shared:
        return std::string("shared i31");
      case kStructShared:
        return std::string("shared struct");
      case kArrayShared:
        return std::string("shared array");
      case kExternShared:
        return std::string("shared extern");
      case kExternStringShared:
        return std::string("shared <extern_string>");
      case kAnyShared:
        return std::string("shared any");
      case kStringShared:
        return std::string("shared string");
      case kStringViewWtf8Shared:
        return std::string("shared stringview_wtf8");
      case kStringViewWtf16Shared:
        return std::string("shared stringview_wtf16");
      case kStringViewIterShared:
        return std::string("shared stringview_iter");
      case kNoneShared:
        return std::string("shared none");
      case kNoExternShared:
        return std::string("shared noextern");
      case kNoFuncShared:
        return std::string("shared nofunc");
      case kNoExnShared:
        return std::string("shared noexn");
      case kExnShared:
        return std::string("shared exn");
      case kBottom:
        return std::string("<bot>");
      default:
        DCHECK(is_index());
        return std::to_string(representation_);
    }
  }

  // Returns the code that represents this heap type in the wasm binary format.
  constexpr int32_t code() const {
    // Type codes represent the first byte of the LEB128 encoding. To get the
    // int32 represented by a code, we need to sign-extend it from 7 to 32 bits.
    int32_t mask = 0xFFFFFF80;
    switch (representation_) {
      case kFunc:
      case kFuncShared:
        return mask | kFuncRefCode;
      case kEq:
      case kEqShared:
        return mask | kEqRefCode;
      case kI31:
      case kI31Shared:
        return mask | kI31RefCode;
      case kStruct:
      case kStructShared:
        return mask | kStructRefCode;
      case kArray:
      case kArrayShared:
        return mask | kArrayRefCode;
      case kExtern:
      case kExternShared:
        return mask | kExternRefCode;
      case kAny:
      case kAnyShared:
        return mask | kAnyRefCode;
      case kExn:
      case kExnShared:
        return mask | kExnRefCode;
      case kString:
      case kStringShared:
        return mask | kStringRefCode;
      case kStringViewWtf8:
      case kStringViewWtf8Shared:
        return mask | kStringViewWtf8Code;
      case kStringViewWtf16:
      case kStringViewWtf16Shared:
        return mask | kStringViewWtf16Code;
      case kStringViewIter:
      case kStringViewIterShared:
        return mask | kStringViewIterCode;
      case kNone:
      case kNoneShared:
        return mask | kNoneCode;
      case kNoExtern:
      case kNoExternShared:
        return mask | kNoExternCode;
      case kNoFunc:
      case kNoFuncShared:
        return mask | kNoFuncCode;
      case kNoExn:
      case kNoExnShared:
        return mask | kNoExnCode;
      default:
        DCHECK(is_index());
        return static_cast<int32_t>(representation_);
    }
  }

  constexpr Representation representation_non_shared() const {
    switch (representation_) {
      case kFuncShared:
        return kFunc;
      case kEqShared:
        return kEq;
      case kI31Shared:
        return kI31;
      case kStructShared:
        return kStruct;
      case kArrayShared:
        return kArray;
      case kAnyShared:
        return kAny;
      case kExternShared:
        return kExtern;
      case kExternStringShared:
        return kExternString;
      case kExnShared:
        return kExn;
      case kStringShared:
        return kString;
      case kStringViewWtf8Shared:
        return kStringViewWtf8;
      case kStringViewWtf16Shared:
        return kStringViewWtf16;
      case kStringViewIterShared:
        return kStringViewIter;
      case kNoneShared:
        return kNone;
      case kNoFuncShared:
        return kNoFunc;
      case kNoExternShared:
        return kNoExtern;
      case kNoExnShared:
        return kNoExn;
      default:
        return representation_;
    }
  }

  constexpr bool is_abstract_shared() const {
    switch (representation_) {
      case kFuncShared:
      case kEqShared:
      case kI31Shared:
      case kStructShared:
      case kArrayShared:
      case kAnyShared:
      case kExternShared:
      case kExternStringShared:
      case kExnShared:
      case kStringShared:
      case kStringViewWtf8Shared:
      case kStringViewWtf16Shared:
      case kStringViewIterShared:
      case kNoneShared:
      case kNoFuncShared:
      case kNoExternShared:
      case kNoExnShared:
        return true;
      default:
        DCHECK(is_abstract_non_shared() || is_index());
        return false;
    }
  }

  constexpr bool is_abstract_non_shared() const {
    switch (representation_) {
      case kFunc:
      case kEq:
      case kI31:
      case kStruct:
      case kArray:
      case kAny:
      case kExtern:
      case kExternString:
      case kExn:
      case kString:
      case kStringViewWtf8:
      case kStringViewWtf16:
      case kStringViewIter:
      case kNone:
      case kNoFunc:
      case kNoExtern:
      case kNoExn:
      case kBottom:
        return true;
      default:
        return false;
    }
  }

 private:
  friend class ValueType;

  constexpr bool is_valid() const { return representation_ <= kLastSentinel; }

  static constexpr Representation kFirstSentinel =
      static_cast<Representation>(kV8MaxWasmTypes);
  static constexpr Representation kLastSentinel =
      static_cast<Representation>(kBottom - 1);
  Representation representation_;
};

enum Nullability : bool { kNonNullable, kNullable };

enum ValueKind : uint8_t {
#define DEF_ENUM(kind, ...) k##kind,
  FOREACH_VALUE_TYPE(DEF_ENUM)
#undef DEF_ENUM
};

constexpr bool is_numeric(ValueKind kind) {
  switch (kind) {
#define NUMERIC_CASE(kind, ...) \
  case k##kind:                 \
    return true;
    FOREACH_NUMERIC_VALUE_TYPE(NUMERIC_CASE)
#undef NUMERIC_CASE
    default:
      return false;
  }
}

constexpr bool is_valid(ValueKind kind) { return kind <= kBottom; }

constexpr bool is_reference(ValueKind kind) {
  return kind == kRef || kind == kRefNull || kind == kRtt;
}

constexpr bool is_object_reference(ValueKind kind) {
  return kind == kRef || kind == kRefNull;
}

constexpr int value_kind_size_log2(ValueKind kind) {
  constexpr int8_t kValueKindSizeLog2[] = {
#define VALUE_KIND_SIZE_LOG2(kind, log2Size, ...) log2Size,
      FOREACH_VALUE_TYPE(VALUE_KIND_SIZE_LOG2)
#undef VALUE_KIND_SIZE_LOG2
  };

  int size_log_2 = kValueKindSizeLog2[kind];
  DCHECK_LE(0, size_log_2);
  return size_log_2;
}

constexpr int value_kind_size(ValueKind kind) {
  constexpr int8_t kElementSize[] = {
#define ELEM_SIZE_LOG2(kind, log2Size, ...) \
  log2Size == -1 ? -1 : (1 << std::max(0, log2Size)),
      FOREACH_VALUE_TYPE(ELEM_SIZE_LOG2)
#undef ELEM_SIZE_LOG2
  };

  int size = kElementSize[kind];
  DCHECK_LT(0, size);
  return size;
}

constexpr int value_kind_full_size(ValueKind kind) {
  if (is_reference(kind)) {
    // Uncompressed pointer size.
    return kSystemPointerSize;
  }
  return value_kind_size(kind);
}

constexpr char short_name(ValueKind kind) {
  constexpr char kShortName[] = {
#define SHORT_NAME(kind, log2Size, code, machineType, shortName, ...) shortName,
      FOREACH_VALUE_TYPE(SHORT_NAME)
#undef SHORT_NAME
  };

  return kShortName[kind];
}

constexpr const char* name(ValueKind kind) {
  constexpr const char* kKindName[] = {
#define KIND_NAME(kind, log2Size, code, machineType, shortName, kindName, ...) \
  kindName,
      FOREACH_VALUE_TYPE(KIND_NAME)
#undef TYPE_NAME
  };

  return kKindName[kind];
}

// Output operator, useful for DCHECKS and others.
inline std::ostream& operator<<(std::ostream& oss, ValueKind kind) {
  return oss << name(kind);
}

constexpr MachineType machine_type(ValueKind kind) {
  DCHECK_NE(kBottom, kind);

  constexpr MachineType kMachineType[] = {
#define MACH_TYPE(kind, log2Size, code, machineType, ...) \
  MachineType::machineType(),
      FOREACH_VALUE_TYPE(MACH_TYPE)
#undef MACH_TYPE
  };

  return kMachineType[kind];
}

constexpr bool is_packed(ValueKind kind) {
  return kind == kI8 || kind == kI16 || kind == kF16;
}
constexpr ValueKind unpacked(ValueKind kind) {
  return is_packed(kind) ? (kind == kF16 ? kF32 : kI32) : kind;
}

constexpr bool is_rtt(ValueKind kind) { return kind == kRtt; }

constexpr bool is_defaultable(ValueKind kind) {
  DCHECK(kind != kBottom && kind != kVoid);
  return kind != kRef && !is_rtt(kind);
}

// A ValueType is encoded by two components: a ValueKind and a heap
// representation (for reference types/rtts). Those are encoded into 32 bits
// using base::BitField. The underlying ValueKind enumeration includes four
// elements which do not strictly correspond to value types: the two packed
// types i8 and i16, the void type (for control structures), and a bottom value
// (for internal use).
// ValueType encoding includes an additional bit marking the index of a type as
// relative. This should only be used during type canonicalization.
class ValueType {
 public:
  /******************************* Constructors *******************************/
  constexpr ValueType() : bit_field_(KindField::encode(kVoid)) {}
  static constexpr ValueType Primitive(ValueKind kind) {
    DCHECK(kind == kBottom || kind <= kF16);
    return ValueType(KindField::encode(kind));
  }
  static constexpr ValueType Ref(uint32_t heap_type) {
    DCHECK(HeapType(heap_type).is_valid());
    return ValueType(KindField::encode(kRef) |
                     HeapTypeField::encode(heap_type));
  }
  static constexpr ValueType Ref(HeapType heap_type) {
    return Ref(heap_type.representation());
  }
  static constexpr ValueType RefNull(uint32_t heap_type) {
    DCHECK(HeapType(heap_type).is_valid());
    return ValueType(KindField::encode(kRefNull) |
                     HeapTypeField::encode(heap_type));
  }
  static constexpr ValueType RefNull(HeapType heap_type) {
    return RefNull(heap_type.representation());
  }
  static constexpr ValueType RefMaybeNull(uint32_t heap_type,
                                          Nullability nullability) {
    DCHECK(HeapType(heap_type).is_valid());
    return ValueType(
        KindField::encode(nullability == kNullable ? kRefNull : kRef) |
        HeapTypeField::encode(heap_type));
  }
  static constexpr ValueType RefMaybeNull(HeapType heap_type,
                                          Nullability nullability) {
    return RefMaybeNull(heap_type.representation(), nullability);
  }

  static constexpr ValueType Rtt(uint32_t type_index) {
    DCHECK(HeapType(type_index).is_index());
    return ValueType(KindField::encode(kRtt) |
                     HeapTypeField::encode(type_index));
  }

  static constexpr ValueType FromIndex(ValueKind kind, uint32_t index) {
    DCHECK(kind == kRefNull || kind == kRef || kind == kRtt);
    CHECK_LT(index, kV8MaxWasmTypes);
    return ValueType(KindField::encode(kind) | HeapTypeField::encode(index));
  }

  // Useful when deserializing a type stored in a runtime object.
  static constexpr ValueType FromRawBitField(uint32_t bit_field) {
    return ValueType(bit_field);
  }

  /******************************** Type checks *******************************/
  // Includes s128.
  constexpr bool is_numeric() const { return wasm::is_numeric(kind()); }

  constexpr bool is_reference() const { return wasm::is_reference(kind()); }

  constexpr bool is_object_reference() const {
    return wasm::is_object_reference(kind());
  }

  constexpr bool is_nullable() const { return kind() == kRefNull; }
  constexpr bool is_non_nullable() const { return kind() == kRef; }

  constexpr bool is_reference_to(uint32_t htype) const {
    return (kind() == kRef || kind() == kRefNull) &&
           heap_representation() == htype;
  }

  constexpr bool is_rtt() const { return wasm::is_rtt(kind()); }

  constexpr bool has_index() const {
    return is_rtt() || (is_object_reference() && heap_type().is_index());
  }

  constexpr bool is_defaultable() const { return wasm::is_defaultable(kind()); }

  constexpr bool is_bottom() const { return kind() == kBottom; }

  constexpr bool is_string_view() const {
    return is_object_reference() && heap_type().is_string_view();
  }

  // Except for {bottom}, these can occur as the result of trapping type casts,
  // type propagation, or trivially uninhabitable parameters/locals, but never
  // in reachable control flow.
  constexpr bool is_uninhabited() const {
    return is_bottom() ||
           (is_non_nullable() && (is_reference_to(HeapType::kNone) ||
                                  is_reference_to(HeapType::kNoExtern) ||
                                  is_reference_to(HeapType::kNoFunc) ||
                                  is_reference_to(HeapType::kNoneShared) ||
                                  is_reference_to(HeapType::kNoExternShared) ||
                                  is_reference_to(HeapType::kNoFuncShared)));
  }

  constexpr bool is_packed() const { return wasm::is_packed(kind()); }

  constexpr ValueType Unpacked() const {
    return is_packed() ? Primitive(kI32) : *this;
  }

  // If {this} is (ref null $t), returns (ref $t). Otherwise, returns {this}.
  constexpr ValueType AsNonNull() const {
    return is_nullable() ? Ref(heap_type()) : *this;
  }

  // If {this} is (ref $t), returns (ref null $t). Otherwise, returns {this}.
  constexpr ValueType AsNullable() const {
    return is_non_nullable() ? RefNull(heap_type()) : *this;
  }

  /***************************** Field Accessors ******************************/
  constexpr ValueKind kind() const { return KindField::decode(bit_field_); }
  constexpr HeapType::Representation heap_representation() const {
    DCHECK(is_object_reference());
    return static_cast<HeapType::Representation>(
        HeapTypeField::decode(bit_field_));
  }
  constexpr HeapType::Representation heap_representation_non_shared() const {
    DCHECK(is_object_reference());
    return HeapType(heap_representation()).representation_non_shared();
  }
  constexpr HeapType heap_type() const {
    DCHECK(is_object_reference());
    return HeapType(heap_representation());
  }
  constexpr uint32_t ref_index() const {
    DCHECK(has_index());
    return HeapTypeField::decode(bit_field_);
  }
  constexpr Nullability nullability() const {
    DCHECK(is_object_reference());
    return kind() == kRefNull ? kNullable : kNonNullable;
  }

  // Useful when serializing this type to store it into a runtime object.
  constexpr uint32_t raw_bit_field() const { return bit_field_; }

  /*************************** Other utility methods **************************/
  constexpr bool operator==(ValueType other) const {
    return bit_field_ == other.bit_field_;
  }
  constexpr bool operator!=(ValueType other) const {
    return bit_field_ != other.bit_field_;
  }

  static constexpr size_t bit_field_offset() {
    return offsetof(ValueType, bit_field_);
  }

  constexpr int value_kind_size_log2() const {
    return wasm::value_kind_size_log2(kind());
  }

  constexpr int value_kind_size() const {
    return wasm::value_kind_size(kind());
  }

  constexpr int value_kind_full_size() const {
    return wasm::value_kind_full_size(kind());
  }

  /*************************** Machine-type related ***************************/
  constexpr MachineType machine_type() const {
    return wasm::machine_type(kind());
  }

  constexpr MachineRepresentation machine_representation() const {
    return machine_type().representation();
  }

  static ValueType For(MachineType type) {
    switch (type.representation()) {
      case MachineRepresentation::kWord8:
      case MachineRepresentation::kWord16:
      case MachineRepresentation::kWord32:
        return Primitive(kI32);
      case MachineRepresentation::kWord64:
        return Primitive(kI64);
      case MachineRepresentation::kFloat32:
        return Primitive(kF32);
      case MachineRepresentation::kFloat64:
        return Primitive(kF64);
      case MachineRepresentation::kTaggedPointer:
        return RefNull(HeapType::kAny);
      case MachineRepresentation::kSimd128:
        return Primitive(kS128);
      default:
        UNREACHABLE();
    }
  }

  /********************************* Encoding *********************************/

  // Returns the first byte of this type's representation in the wasm binary
  // format.
  // For compatibility with the reftypes and exception-handling proposals, this
  // function prioritizes shorthand encodings
  // (e.g., {Ref(HeapType::kFunc, kNullable).value_type_code()} will return
  // kFuncrefCode and not kRefNullCode).
  constexpr ValueTypeCode value_type_code() const {
    DCHECK_NE(kBottom, kind());
    switch (kind()) {
      case kRefNull:
        switch (heap_representation()) {
          case HeapType::kFunc:
            return kFuncRefCode;
          case HeapType::kEq:
            return kEqRefCode;
          case HeapType::kExtern:
            return kExternRefCode;
          case HeapType::kAny:
            return kAnyRefCode;
          case HeapType::kExn:
            return kExnRefCode;
          case HeapType::kI31:
            return kI31RefCode;
          case HeapType::kStruct:
            return kStructRefCode;
          case HeapType::kArray:
            return kArrayRefCode;
          case HeapType::kString:
            return kStringRefCode;
          case HeapType::kNone:
            return kNoneCode;
          case HeapType::kNoExtern:
            return kNoExternCode;
          case HeapType::kNoFunc:
            return kNoFuncCode;
          default:
            return kRefNullCode;
        }
      case kRef:
        switch (heap_representation()) {
          // String views are non-nullable references.
          case HeapType::kStringViewWtf8:
            return kStringViewWtf8Code;
          case HeapType::kStringViewWtf16:
            return kStringViewWtf16Code;
          case HeapType::kStringViewIter:
            return kStringViewIterCode;
          // Currently, no other non-nullable shorthands exist.
          default:
            return kRefCode;
        }
#define NUMERIC_TYPE_CASE(kind, ...) \
  case k##kind:                      \
    return k##kind##Code;
        FOREACH_NUMERIC_VALUE_TYPE(NUMERIC_TYPE_CASE)
#undef NUMERIC_TYPE_CASE
      // The RTT value type can not be used in WebAssembly and is a
      // compiler-internal type only.
      case kRtt:
      case kVoid:
      case kBottom:
        // Unreachable code
        return kVoidCode;
    }
  }

  // Returns true iff the heap type is needed to encode this type in the wasm
  // binary format, taking into account available type shorthands.
  constexpr bool encoding_needs_heap_type() const {
    return kind() == kRef ||
           (kind() == kRefNull && !heap_type().is_abstract_non_shared());
  }

  constexpr bool encoding_needs_shared() const {
    return is_object_reference() && heap_type().is_abstract_shared();
  }

  /****************************** Pretty-printing *****************************/
  constexpr char short_name() const { return wasm::short_name(kind()); }

  std::string name() const {
    std::ostringstream buf;
    switch (kind()) {
      case kRef:
        buf << "(ref " << heap_type().name() << ")";
        break;
      case kRefNull:
        if (heap_type().is_abstract_non_shared() &&
            !heap_type().is_string_view()) {
          switch (heap_type().representation()) {
            case HeapType::kNone:
              buf << "nullref";
              break;
            case HeapType::kNoExtern:
              buf << "nullexternref";
              break;
            case HeapType::kNoFunc:
              buf << "nullfuncref";
              break;
            default:
              buf << heap_type().name() << "ref";
              break;
          }
        } else {
          buf << "(ref null " << heap_type().name() << ")";
        }
        break;
      case kRtt:
        buf << "(rtt " << ref_index() << ")";
        break;
      default:
        buf << kind_name();
    }
    return buf.str();
  }

  /********************** Type canonicalization utilities *********************/
  static constexpr ValueType CanonicalWithRelativeIndex(ValueKind kind,
                                                        uint32_t index) {
    return ValueType(KindField::encode(kind) | HeapTypeField::encode(index) |
                     CanonicalRelativeField::encode(true));
  }

  constexpr bool is_canonical_relative() const {
    return has_index() && CanonicalRelativeField::decode(bit_field_);
  }

  /**************************** Static constants ******************************/
  static constexpr int kLastUsedBit = 25;
  static constexpr int kKindBits = 5;
  static constexpr int kHeapTypeBits = 20;

  static const intptr_t kBitFieldOffset;

 private:
  // {hash_value} directly reads {bit_field_}.
  friend size_t hash_value(ValueType type);

  using KindField = base::BitField<ValueKind, 0, kKindBits>;
  using HeapTypeField = KindField::Next<uint32_t, kHeapTypeBits>;
  // Marks a type as a canonical type which uses an index relative to its
  // recursive group start. Used only during type canonicalization.
  using CanonicalRelativeField = HeapTypeField::Next<bool, 1>;

  static_assert(kV8MaxWasmTypes < (1u << kHeapTypeBits),
                "Type indices fit in kHeapTypeBits");
  // This is implemented defensively against field order changes.
  static_assert(kLastUsedBit ==
                    std::max(KindField::kLastUsedBit,
                             std::max(HeapTypeField::kLastUsedBit,
                                      CanonicalRelativeField::kLastUsedBit)),
                "kLastUsedBit is consistent");

  constexpr explicit ValueType(uint32_t bit_field) : bit_field_(bit_field) {}

  constexpr const char* kind_name() const { return wasm::name(kind()); }

  uint32_t bit_field_;
};
ASSERT_TRIVIALLY_COPYABLE(ValueType);

inline constexpr intptr_t ValueType::kBitFieldOffset =
    offsetof(ValueType, bit_field_);

static_assert(sizeof(ValueType) <= kUInt32Size,
              "ValueType is small and can be passed by value");
static_assert(ValueType::kLastUsedBit < 8 * sizeof(ValueType) - kSmiTagSize,
              "ValueType has space to be encoded in a Smi");

inline size_t hash_value(ValueType type) {
  // Just use the whole encoded bit field, similar to {operator==}.
  return static_cast<size_t>(type.bit_field_);
}

// Output operator, useful for DCHECKS and others.
inline std::ostream& operator<<(std::ostream& oss, ValueType type) {
  return oss << type.name();
}

// Precomputed primitive types.
constexpr ValueType kWasmI32 = ValueType::Primitive(kI32);
constexpr ValueType kWasmI64 = ValueType::Primitive(kI64);
constexpr ValueType kWasmF32 = ValueType::Primitive(kF32);
constexpr ValueType kWasmF64 = ValueType::Primitive(kF64);
constexpr ValueType kWasmS128 = ValueType::Primitive(kS128);
constexpr ValueType kWasmI8 = ValueType::Primitive(kI8);
constexpr ValueType kWasmI16 = ValueType::Primitive(kI16);
constexpr ValueType kWasmF16 = ValueType::Primitive(kF16);
constexpr ValueType kWasmVoid = ValueType::Primitive(kVoid);
constexpr ValueType kWasmBottom = ValueType::Primitive(kBottom);
// Established reference-type and wasm-gc proposal shorthands.
constexpr ValueType kWasmFuncRef = ValueType::RefNull(HeapType::kFunc);
constexpr ValueType kWasmAnyRef = ValueType::RefNull(HeapType::kAny);
constexpr ValueType kWasmExternRef = ValueType::RefNull(HeapType::kExtern);
constexpr ValueType kWasmExnRef = ValueType::RefNull(HeapType::kExn);
constexpr ValueType kWasmEqRef = ValueType::RefNull(HeapType::kEq);
constexpr ValueType kWasmI31Ref = ValueType::RefNull(HeapType::kI31);
constexpr ValueType kWasmStructRef = ValueType::RefNull(HeapType::kStruct);
constexpr ValueType kWasmArrayRef = ValueType::RefNull(HeapType::kArray);
constexpr ValueType kWasmStringRef = ValueType::RefNull(HeapType::kString);
constexpr ValueType kWasmRefString = ValueType::Ref(HeapType::kString);
constexpr ValueType kWasmRefNullExternString =
    ValueType::RefNull(HeapType::kExternString);
constexpr ValueType kWasmRefExternString =
    ValueType::Ref(HeapType::kExternString);
constexpr ValueType kWasmStringViewWtf8 =
    ValueType::Ref(HeapType::kStringViewWtf8);
constexpr ValueType kWasmStringViewWtf16 =
    ValueType::Ref(HeapType::kStringViewWtf16);
constexpr ValueType kWasmStringViewIter =
    ValueType::Ref(HeapType::kStringViewIter);
constexpr ValueType kWasmNullRef = ValueType::RefNull(HeapType::kNone);
constexpr ValueType kWasmNullExternRef =
    ValueType::RefNull(HeapType::kNoExtern);
constexpr ValueType kWasmNullExnRef = ValueType::RefNull(HeapType::kNoExn);
constexpr ValueType kWasmNullFuncRef = ValueType::RefNull(HeapType::kNoFunc);

// Constants used by the generic js-to-wasm wrapper.
constexpr int kWasmValueKindBitsMask = (1u << ValueType::kKindBits) - 1;
constexpr int kWasmHeapTypeBitsMask = (1u << ValueType::kHeapTypeBits) - 1;

#define FOREACH_WASMVALUE_CTYPES(V) \
  V(kI32, int32_t)                  \
  V(kI64, int64_t)                  \
  V(kF32, float)                    \
  V(kF64, double)                   \
  V(kS128, Simd128)

using FunctionSig = Signature<ValueType>;

#define FOREACH_LOAD_TYPE(V) \
  V(I32, , Int32)            \
  V(I32, 8S, Int8)           \
  V(I32, 8U, Uint8)          \
  V(I32, 16S, Int16)         \
  V(I32, 16U, Uint16)        \
  V(I64, , Int64)            \
  V(I64, 8S, Int8)           \
  V(I64, 8U, Uint8)          \
  V(I64, 16S, Int16)         \
  V(I64, 16U, Uint16)        \
  V(I64, 32S, Int32)         \
  V(I64, 32U, Uint32)        \
  V(F32, F16, Float16)       \
  V(F32, , Float32)          \
  V(F64, , Float64)          \
  V(S128, , Simd128)

class LoadType {
 public:
  enum LoadTypeValue : uint8_t {
#define DEF_ENUM(type, suffix, ...) k##type##Load##suffix,
    FOREACH_LOAD_TYPE(DEF_ENUM)
#undef DEF_ENUM
  };

  // Allow implicit conversion of the enum value to this wrapper.
  constexpr LoadType(LoadTypeValue val)  // NOLINT(runtime/explicit)
      : val_(val) {}

  constexpr LoadTypeValue value() const { return val_; }
  constexpr uint8_t size_log_2() const { return kLoadSizeLog2[val_]; }
  constexpr uint8_t size() const { return kLoadSize[val_]; }
  constexpr ValueType value_type() const { return kValueType[val_]; }
  constexpr MachineType mem_type() const { return kMemType[val_]; }

  static LoadType ForValueKind(ValueKind kind, bool is_signed = false) {
    switch (kind) {
      case kI32:
        return kI32Load;
      case kI64:
        return kI64Load;
      case kF32:
        return kF32Load;
      case kF64:
        return kF64Load;
      case kS128:
        return kS128Load;
      case kI8:
        return is_signed ? kI32Load8S : kI32Load8U;
      case kI16:
        return is_signed ? kI32Load16S : kI32Load16U;
      case kF16:
        return kF32LoadF16;
      default:
        UNREACHABLE();
    }
  }

 private:
  LoadTypeValue val_;

  static constexpr uint8_t kLoadSize[] = {
  // MSVC wants a static_cast here.
#define LOAD_SIZE(_, __, memtype) \
  static_cast<uint8_t>(           \
      ElementSizeInBytes(MachineType::memtype().representation())),
      FOREACH_LOAD_TYPE(LOAD_SIZE)
#undef LOAD_SIZE
  };

  static constexpr uint8_t kLoadSizeLog2[] = {
  // MSVC wants a static_cast here.
#define LOAD_SIZE(_, __, memtype) \
  static_cast<uint8_t>(           \
      ElementSizeLog2Of(MachineType::memtype().representation())),
      FOREACH_LOAD_TYPE(LOAD_SIZE)
#undef LOAD_SIZE
  };

  static constexpr ValueType kValueType[] = {
#define VALUE_TYPE(type, ...) ValueType::Primitive(k##type),
      FOREACH_LOAD_TYPE(VALUE_TYPE)
#undef VALUE_TYPE
  };

  static constexpr MachineType kMemType[] = {
#define MEMTYPE(_, __, memtype) MachineType::memtype(),
      FOREACH_LOAD_TYPE(MEMTYPE)
#undef MEMTYPE
  };
};

#define FOREACH_STORE_TYPE(V) \
  V(I32, , Word32)            \
  V(I32, 8, Word8)            \
  V(I32, 16, Word16)          \
  V(I64, , Word64)            \
  V(I64, 8, Word8)            \
  V(I64, 16, Word16)          \
  V(I64, 32, Word32)          \
  V(F32, F16, Float16)        \
  V(F32, , Float32)           \
  V(F64, , Float64)           \
  V(S128, , Simd128)

class StoreType {
 public:
  enum StoreTypeValue : uint8_t {
#define DEF_ENUM(type, suffix, ...) k##type##Store##suffix,
    FOREACH_STORE_TYPE(DEF_ENUM)
#undef DEF_ENUM
  };

  // Allow implicit convertion of the enum value to this wrapper.
  constexpr StoreType(StoreTypeValue val)  // NOLINT(runtime/explicit)
      : val_(val) {}

  constexpr StoreTypeValue value() const { return val_; }
  constexpr unsigned size_log_2() const { return kStoreSizeLog2[val_]; }
  constexpr unsigned size() const { return 1 << size_log_2(); }
  constexpr ValueType value_type() const { return kValueType[val_]; }
  constexpr MachineRepresentation mem_rep() const { return kMemRep[val_]; }

  static StoreType ForValueKind(ValueKind kind) {
    switch (kind) {
      case kI32:
        return kI32Store;
      case kI64:
        return kI64Store;
      case kF32:
        return kF32Store;
      case kF64:
        return kF64Store;
      case kS128:
        return kS128Store;
      case kI8:
        return kI32Store8;
      case kI16:
        return kI32Store16;
      case kF16:
        return kF32StoreF16;
      default:
        UNREACHABLE();
    }
  }

 private:
  StoreTypeValue val_;

  static constexpr uint8_t kStoreSizeLog2[] = {
  // MSVC wants a static_cast here.
#define STORE_SIZE(_, __, memrep) \
  static_cast<uint8_t>(ElementSizeLog2Of(MachineRepresentation::k##memrep)),
      FOREACH_STORE_TYPE(STORE_SIZE)
#undef STORE_SIZE
  };

  static constexpr ValueType kValueType[] = {
#define VALUE_TYPE(type, ...) ValueType::Primitive(k##type),
      FOREACH_STORE_TYPE(VALUE_TYPE)
#undef VALUE_TYPE
  };

  static constexpr MachineRepresentation kMemRep[] = {
#define MEMREP(_, __, memrep) MachineRepresentation::k##memrep,
      FOREACH_STORE_TYPE(MEMREP)
#undef MEMREP
  };
};

std::optional<wasm::ValueKind> WasmReturnTypeFromSignature(
    const FunctionSig* wasm_signature);

// Lowers a signature for 32 bit platforms by replacing i64 parameters and
// returns with two i32s each.
V8_EXPORT_PRIVATE const wasm::FunctionSig* GetI32Sig(
    Zone* zone, const wasm::FunctionSig* sig);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_VALUE_TYPE_H_
