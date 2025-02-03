// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_INIT_EXPR_H_
#define V8_WASM_WASM_INIT_EXPR_H_

#include <memory>

#include "src/wasm/value-type.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace wasm {

struct WasmModule;

// Representation of an constant expression. Unlike {ConstantExpression}, this
// does not use {WireBytesRef}, i.e., it does not depend on a wasm module's
// bytecode representation.
class WasmInitExpr : public ZoneObject {
 public:
  enum Operator {
    kGlobalGet,
    kI32Const,
    kI64Const,
    kF32Const,
    kF64Const,
    kS128Const,
    kI32Add,
    kI32Sub,
    kI32Mul,
    kI64Add,
    kI64Sub,
    kI64Mul,
    kRefNullConst,
    kRefFuncConst,
    kStructNew,
    kStructNewDefault,
    kArrayNew,
    kArrayNewDefault,
    kArrayNewFixed,
    kRefI31,
    kStringConst,
    kAnyConvertExtern,
    kExternConvertAny
  };

  union Immediate {
    int32_t i32_const;
    int64_t i64_const;
    float f32_const;
    double f64_const;
    std::array<uint8_t, kSimd128Size> s128_const;
    uint32_t index;
    HeapType::Representation heap_type;
  };

  explicit WasmInitExpr(int32_t v) : kind_(kI32Const), operands_(nullptr) {
    immediate_.i32_const = v;
  }
  explicit WasmInitExpr(int64_t v) : kind_(kI64Const), operands_(nullptr) {
    immediate_.i64_const = v;
  }
  explicit WasmInitExpr(float v) : kind_(kF32Const), operands_(nullptr) {
    immediate_.f32_const = v;
  }
  explicit WasmInitExpr(double v) : kind_(kF64Const), operands_(nullptr) {
    immediate_.f64_const = v;
  }
  explicit WasmInitExpr(uint8_t v[kSimd128Size])
      : kind_(kS128Const), operands_(nullptr) {
    memcpy(immediate_.s128_const.data(), v, kSimd128Size);
  }

  static WasmInitExpr Binop(Zone* zone, Operator op, WasmInitExpr lhs,
                            WasmInitExpr rhs) {
    DCHECK(op == kI32Add || op == kI32Sub || op == kI32Mul || op == kI64Add ||
           op == kI64Sub || op == kI64Mul);
    return WasmInitExpr(zone, op, {lhs, rhs});
  }

  static WasmInitExpr GlobalGet(uint32_t index) {
    WasmInitExpr expr(kGlobalGet);
    expr.immediate_.index = index;
    return expr;
  }

  static WasmInitExpr RefFuncConst(uint32_t index) {
    WasmInitExpr expr(kRefFuncConst);
    expr.immediate_.index = index;
    return expr;
  }

  static WasmInitExpr RefNullConst(HeapType::Representation heap_type) {
    WasmInitExpr expr(kRefNullConst);
    expr.immediate_.heap_type = heap_type;
    return expr;
  }

  static WasmInitExpr StructNew(uint32_t index,
                                ZoneVector<WasmInitExpr>* elements) {
    WasmInitExpr expr(kStructNew, elements);
    expr.immediate_.index = index;
    return expr;
  }

  static WasmInitExpr StructNewDefault(uint32_t index) {
    WasmInitExpr expr(kStructNewDefault);
    expr.immediate_.index = index;
    return expr;
  }

  static WasmInitExpr ArrayNew(Zone* zone, uint32_t index, WasmInitExpr initial,
                               WasmInitExpr length) {
    WasmInitExpr expr(zone, kArrayNew, {initial, length});
    expr.immediate_.index = index;
    return expr;
  }

  static WasmInitExpr ArrayNewDefault(Zone* zone, uint32_t index,
                                      WasmInitExpr length) {
    WasmInitExpr expr(zone, kArrayNewDefault, {length});
    expr.immediate_.index = index;
    return expr;
  }

  static WasmInitExpr ArrayNewFixed(uint32_t index,
                                    ZoneVector<WasmInitExpr>* elements) {
    WasmInitExpr expr(kArrayNewFixed, elements);
    expr.immediate_.index = index;
    return expr;
  }

  static WasmInitExpr RefI31(Zone* zone, WasmInitExpr value) {
    WasmInitExpr expr(zone, kRefI31, {value});
    return expr;
  }

  static WasmInitExpr StringConst(uint32_t index) {
    WasmInitExpr expr(kStringConst);
    expr.immediate_.index = index;
    return expr;
  }

  static WasmInitExpr AnyConvertExtern(Zone* zone, WasmInitExpr arg) {
    return WasmInitExpr(zone, kAnyConvertExtern, {arg});
  }

  static WasmInitExpr ExternConvertAny(Zone* zone, WasmInitExpr arg) {
    return WasmInitExpr(zone, kExternConvertAny, {arg});
  }

  Immediate immediate() const { return immediate_; }
  Operator kind() const { return kind_; }
  const ZoneVector<WasmInitExpr>* operands() const { return operands_; }

  bool operator==(const WasmInitExpr& other) const {
    if (kind() != other.kind()) return false;
    switch (kind()) {
      case kGlobalGet:
      case kRefFuncConst:
      case kStringConst:
        return immediate().index == other.immediate().index;
      case kI32Const:
        return immediate().i32_const == other.immediate().i32_const;
      case kI64Const:
        return immediate().i64_const == other.immediate().i64_const;
      case kF32Const:
        return immediate().f32_const == other.immediate().f32_const;
      case kF64Const:
        return immediate().f64_const == other.immediate().f64_const;
      case kI32Add:
      case kI32Sub:
      case kI32Mul:
      case kI64Add:
      case kI64Sub:
      case kI64Mul:
        return operands_[0] == other.operands_[0] &&
               operands_[1] == other.operands_[1];
      case kS128Const:
        return immediate().s128_const == other.immediate().s128_const;
      case kRefNullConst:
        return immediate().heap_type == other.immediate().heap_type;
      case kStructNew:
      case kStructNewDefault:
      case kArrayNew:
      case kArrayNewDefault:
        if (immediate().index != other.immediate().index) return false;
        DCHECK_EQ(operands()->size(), other.operands()->size());
        for (uint32_t i = 0; i < operands()->size(); i++) {
          if (operands()[i] != other.operands()[i]) return false;
        }
        return true;
      case kArrayNewFixed:
        if (immediate().index != other.immediate().index) return false;
        if (operands()->size() != other.operands()->size()) return false;
        for (uint32_t i = 0; i < operands()->size(); i++) {
          if (operands()[i] != other.operands()[i]) return false;
        }
        return true;
      case kRefI31:
      case kAnyConvertExtern:
      case kExternConvertAny:
        return operands_[0] == other.operands_[0];
    }
  }

  V8_INLINE bool operator!=(const WasmInitExpr& other) const {
    return !(*this == other);
  }

  static WasmInitExpr DefaultValue(ValueType type) {
    // No initializer, emit a default value.
    switch (type.kind()) {
      case kI8:
      case kI16:
      case kI32:
        return WasmInitExpr(int32_t{0});
      case kI64:
        return WasmInitExpr(int64_t{0});
      case kF16:
      case kF32:
        return WasmInitExpr(0.0f);
      case kF64:
        return WasmInitExpr(0.0);
      case kRefNull:
        return WasmInitExpr::RefNullConst(type.heap_representation());
      case kS128: {
        uint8_t value[kSimd128Size] = {0};
        return WasmInitExpr(value);
      }
      case kVoid:
      case kBottom:
      case kRef:
      case kRtt:
        UNREACHABLE();
    }
  }

 private:
  WasmInitExpr(Operator kind, const ZoneVector<WasmInitExpr>* operands)
      : kind_(kind), operands_(operands) {}
  explicit WasmInitExpr(Operator kind) : kind_(kind), operands_(nullptr) {}
  WasmInitExpr(Zone* zone, Operator kind,
               std::initializer_list<WasmInitExpr> operands)
      : kind_(kind),
        operands_(zone->New<ZoneVector<WasmInitExpr>>(operands, zone)) {}
  Immediate immediate_;
  Operator kind_;
  const ZoneVector<WasmInitExpr>* operands_;
};

ASSERT_TRIVIALLY_COPYABLE(WasmInitExpr);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_INIT_EXPR_H_
