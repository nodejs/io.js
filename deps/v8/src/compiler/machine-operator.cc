// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/machine-operator.h"

#include <optional>

#include "src/base/lazy-instance.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"

namespace v8 {
namespace internal {
namespace compiler {

bool operator==(StoreRepresentation lhs, StoreRepresentation rhs) {
  return lhs.representation() == rhs.representation() &&
         lhs.write_barrier_kind() == rhs.write_barrier_kind();
}


bool operator!=(StoreRepresentation lhs, StoreRepresentation rhs) {
  return !(lhs == rhs);
}


size_t hash_value(StoreRepresentation rep) {
  return base::hash_combine(rep.representation(), rep.write_barrier_kind());
}


std::ostream& operator<<(std::ostream& os, StoreRepresentation rep) {
  return os << rep.representation() << ", " << rep.write_barrier_kind();
}

bool operator==(AtomicStoreParameters lhs, AtomicStoreParameters rhs) {
  return lhs.store_representation() == rhs.store_representation() &&
         lhs.order() == rhs.order() && lhs.kind() == rhs.kind();
}

bool operator!=(AtomicStoreParameters lhs, AtomicStoreParameters rhs) {
  return !(lhs == rhs);
}

size_t hash_value(AtomicStoreParameters params) {
  return base::hash_combine(hash_value(params.store_representation()),
                            params.order(), params.kind());
}

std::ostream& operator<<(std::ostream& os, AtomicStoreParameters params) {
  return os << params.store_representation() << ", " << params.order();
}

bool operator==(AtomicLoadParameters lhs, AtomicLoadParameters rhs) {
  return lhs.representation() == rhs.representation() &&
         lhs.order() == rhs.order() && lhs.kind() == rhs.kind();
}

bool operator!=(AtomicLoadParameters lhs, AtomicLoadParameters rhs) {
  return !(lhs == rhs);
}

size_t hash_value(AtomicLoadParameters params) {
  return base::hash_combine(params.representation(), params.order(),
                            params.kind());
}

std::ostream& operator<<(std::ostream& os, AtomicLoadParameters params) {
  return os << params.representation() << ", " << params.order();
}

bool operator==(AtomicOpParameters lhs, AtomicOpParameters rhs) {
  return lhs.type() == rhs.type() && lhs.kind() == rhs.kind();
}

bool operator!=(AtomicOpParameters lhs, AtomicOpParameters rhs) {
  return !(lhs == rhs);
}

size_t hash_value(AtomicOpParameters params) {
  return base::hash_combine(params.type(), params.kind());
}

std::ostream& operator<<(std::ostream& os, AtomicOpParameters params) {
  return os << params.type() << ", " << params.kind();
}

size_t hash_value(MemoryAccessKind kind) { return static_cast<size_t>(kind); }

std::ostream& operator<<(std::ostream& os, MemoryAccessKind kind) {
  switch (kind) {
    case MemoryAccessKind::kNormal:
      return os << "kNormal";
    case MemoryAccessKind::kUnaligned:
      return os << "kUnaligned";
    case MemoryAccessKind::kProtected:
      return os << "kProtected";
  }
  UNREACHABLE();
}

size_t hash_value(LoadTransformation rep) { return static_cast<size_t>(rep); }

std::ostream& operator<<(std::ostream& os, LoadTransformation rep) {
  switch (rep) {
    case LoadTransformation::kS128Load8Splat:
      return os << "kS128Load8Splat";
    case LoadTransformation::kS128Load16Splat:
      return os << "kS128Load16Splat";
    case LoadTransformation::kS128Load32Splat:
      return os << "kS128Load32Splat";
    case LoadTransformation::kS128Load64Splat:
      return os << "kS128Load64Splat";
    case LoadTransformation::kS128Load8x8S:
      return os << "kS128Load8x8S";
    case LoadTransformation::kS128Load8x8U:
      return os << "kS128Load8x8U";
    case LoadTransformation::kS128Load16x4S:
      return os << "kS128Load16x4S";
    case LoadTransformation::kS128Load16x4U:
      return os << "kS128Load16x4U";
    case LoadTransformation::kS128Load32x2S:
      return os << "kS128Load32x2S";
    case LoadTransformation::kS128Load32x2U:
      return os << "kS128Load32x2U";
    case LoadTransformation::kS128Load32Zero:
      return os << "kS128Load32Zero";
    case LoadTransformation::kS128Load64Zero:
      return os << "kS128Load64Zero";
    // Simd256
    case LoadTransformation::kS256Load8Splat:
      return os << "kS256Load8Splat";
    case LoadTransformation::kS256Load16Splat:
      return os << "kS256Load16Splat";
    case LoadTransformation::kS256Load32Splat:
      return os << "kS256Load32Splat";
    case LoadTransformation::kS256Load64Splat:
      return os << "kS256Load64Splat";
    case LoadTransformation::kS256Load8x16S:
      return os << "kS256Load8x16S";
    case LoadTransformation::kS256Load8x16U:
      return os << "kS256Load8x16U";
    case LoadTransformation::kS256Load16x8S:
      return os << "kS256Load16x8S";
    case LoadTransformation::kS256Load16x8U:
      return os << "kS256Load16x8U";
    case LoadTransformation::kS256Load32x4S:
      return os << "kS256Load32x4S";
    case LoadTransformation::kS256Load32x4U:
      return os << "kS256Load32x4U";
  }
  UNREACHABLE();
}

size_t hash_value(LoadTransformParameters params) {
  return base::hash_combine(params.kind, params.transformation);
}

std::ostream& operator<<(std::ostream& os, LoadTransformParameters params) {
  return os << "(" << params.kind << " " << params.transformation << ")";
}

#if V8_ENABLE_WEBASSEMBLY
LoadTransformParameters const& LoadTransformParametersOf(Operator const* op) {
  DCHECK_EQ(IrOpcode::kLoadTransform, op->opcode());
  return OpParameter<LoadTransformParameters>(op);
}

bool operator==(LoadTransformParameters lhs, LoadTransformParameters rhs) {
  return lhs.transformation == rhs.transformation && lhs.kind == rhs.kind;
}

bool operator!=(LoadTransformParameters lhs, LoadTransformParameters rhs) {
  return !(lhs == rhs);
}

size_t hash_value(LoadLaneParameters params) {
  return base::hash_combine(params.kind, params.rep, params.laneidx);
}

std::ostream& operator<<(std::ostream& os, LoadLaneParameters params) {
  return os << "(" << params.kind << " " << params.rep << " "
            << static_cast<uint32_t>(params.laneidx) << ")";
}

LoadLaneParameters const& LoadLaneParametersOf(Operator const* op) {
  DCHECK_EQ(IrOpcode::kLoadLane, op->opcode());
  return OpParameter<LoadLaneParameters>(op);
}

bool operator==(LoadLaneParameters lhs, LoadLaneParameters rhs) {
  return lhs.kind == rhs.kind && lhs.rep == rhs.rep &&
         lhs.laneidx == rhs.laneidx;
}

size_t hash_value(StoreLaneParameters params) {
  return base::hash_combine(params.kind, params.rep, params.laneidx);
}

std::ostream& operator<<(std::ostream& os, StoreLaneParameters params) {
  return os << "(" << params.kind << " " << params.rep << " "
            << static_cast<unsigned int>(params.laneidx) << ")";
}

StoreLaneParameters const& StoreLaneParametersOf(Operator const* op) {
  DCHECK_EQ(IrOpcode::kStoreLane, op->opcode());
  return OpParameter<StoreLaneParameters>(op);
}

bool operator==(StoreLaneParameters lhs, StoreLaneParameters rhs) {
  return lhs.kind == rhs.kind && lhs.rep == rhs.rep &&
         lhs.laneidx == rhs.laneidx;
}
#endif  // V8_ENABLE_WEBASSEMBLY

LoadRepresentation LoadRepresentationOf(Operator const* op) {
  DCHECK(IrOpcode::kLoad == op->opcode() ||
         IrOpcode::kProtectedLoad == op->opcode() ||
         IrOpcode::kLoadTrapOnNull == op->opcode() ||
         IrOpcode::kUnalignedLoad == op->opcode() ||
         IrOpcode::kLoadImmutable == op->opcode());
  return OpParameter<LoadRepresentation>(op);
}

AtomicLoadParameters AtomicLoadParametersOf(Operator const* op) {
  DCHECK(IrOpcode::kWord32AtomicLoad == op->opcode() ||
         IrOpcode::kWord64AtomicLoad == op->opcode());
  return OpParameter<AtomicLoadParameters>(op);
}

AtomicOpParameters AtomicOpParametersOf(Operator const* op) {
  DCHECK(IrOpcode::isAtomicOpOpcode(IrOpcode::Value(op->opcode())));
  return OpParameter<AtomicOpParameters>(op);
}

StoreRepresentation const& StoreRepresentationOf(Operator const* op) {
  DCHECK(IrOpcode::kStore == op->opcode() ||
         IrOpcode::kProtectedStore == op->opcode() ||
         IrOpcode::kStoreTrapOnNull == op->opcode() ||
         IrOpcode::kStoreIndirectPointer == op->opcode());
  return OpParameter<StoreRepresentation>(op);
}

StorePairRepresentation const& StorePairRepresentationOf(Operator const* op) {
  DCHECK(IrOpcode::kStorePair == op->opcode());
  return OpParameter<StorePairRepresentation>(op);
}

AtomicStoreParameters const& AtomicStoreParametersOf(Operator const* op) {
  DCHECK(IrOpcode::kWord32AtomicStore == op->opcode() ||
         IrOpcode::kWord64AtomicStore == op->opcode());
  return OpParameter<AtomicStoreParameters>(op);
}

UnalignedStoreRepresentation const& UnalignedStoreRepresentationOf(
    Operator const* op) {
  DCHECK_EQ(IrOpcode::kUnalignedStore, op->opcode());
  return OpParameter<UnalignedStoreRepresentation>(op);
}

bool operator==(StackSlotRepresentation lhs, StackSlotRepresentation rhs) {
  return lhs.size() == rhs.size() && lhs.alignment() == rhs.alignment();
}

bool operator!=(StackSlotRepresentation lhs, StackSlotRepresentation rhs) {
  return !(lhs == rhs);
}

size_t hash_value(StackSlotRepresentation rep) {
  return base::hash_combine(rep.size(), rep.alignment());
}

std::ostream& operator<<(std::ostream& os, StackSlotRepresentation rep) {
  return os << rep.size() << ", " << rep.alignment();
}

StackSlotRepresentation const& StackSlotRepresentationOf(Operator const* op) {
  DCHECK_EQ(IrOpcode::kStackSlot, op->opcode());
  return OpParameter<StackSlotRepresentation>(op);
}

MachineType AtomicOpType(Operator const* op) {
  const AtomicOpParameters params = OpParameter<AtomicOpParameters>(op);
  return params.type();
}

size_t hash_value(ShiftKind kind) { return static_cast<size_t>(kind); }
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os, ShiftKind kind) {
  switch (kind) {
    case ShiftKind::kNormal:
      return os << "Normal";
    case ShiftKind::kShiftOutZeros:
      return os << "ShiftOutZeros";
  }
}

ShiftKind ShiftKindOf(Operator const* op) {
  DCHECK(IrOpcode::kWord32Sar == op->opcode() ||
         IrOpcode::kWord64Sar == op->opcode());
  return OpParameter<ShiftKind>(op);
}

size_t hash_value(TruncateKind kind) { return static_cast<size_t>(kind); }

std::ostream& operator<<(std::ostream& os, TruncateKind kind) {
  switch (kind) {
    case TruncateKind::kArchitectureDefault:
      return os << "kArchitectureDefault";
    case TruncateKind::kSetOverflowToMin:
      return os << "kSetOverflowToMin";
  }
}

// The format is:
// V(Name, properties, value_input_count, control_input_count, output_count)
#define PURE_BINARY_OP_LIST_32(V)                                           \
  V(Word32And, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)    \
  V(Word32Or, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)     \
  V(Word32Xor, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)    \
  V(Word32Shl, Operator::kNoProperties, 2, 0, 1)                            \
  V(Word32Shr, Operator::kNoProperties, 2, 0, 1)                            \
  V(Word32Ror, Operator::kNoProperties, 2, 0, 1)                            \
  V(Word32Equal, Operator::kCommutative, 2, 0, 1)                           \
  V(Int32Add, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)     \
  V(Int32Sub, Operator::kNoProperties, 2, 0, 1)                             \
  V(Int32Mul, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)     \
  V(Int32MulHigh, Operator::kAssociative | Operator::kCommutative, 2, 0, 1) \
  V(Int32Div, Operator::kNoProperties, 2, 1, 1)                             \
  V(Int32Mod, Operator::kNoProperties, 2, 1, 1)                             \
  V(Int32LessThan, Operator::kNoProperties, 2, 0, 1)                        \
  V(Int32LessThanOrEqual, Operator::kNoProperties, 2, 0, 1)                 \
  V(Uint32Div, Operator::kNoProperties, 2, 1, 1)                            \
  V(Uint32LessThan, Operator::kNoProperties, 2, 0, 1)                       \
  V(Uint32LessThanOrEqual, Operator::kNoProperties, 2, 0, 1)                \
  V(Uint32Mod, Operator::kNoProperties, 2, 1, 1)                            \
  V(Uint32MulHigh, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)

// The format is:
// V(Name, properties, value_input_count, control_input_count, output_count)
#define PURE_BINARY_OP_LIST_64(V)                                            \
  V(Word64And, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)     \
  V(Word64Or, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)      \
  V(Word64Xor, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)     \
  V(Word64Shl, Operator::kNoProperties, 2, 0, 1)                             \
  V(Word64Shr, Operator::kNoProperties, 2, 0, 1)                             \
  V(Word64Ror, Operator::kNoProperties, 2, 0, 1)                             \
  V(Word64RorLowerable, Operator::kNoProperties, 2, 1, 1)                    \
  V(Word64Equal, Operator::kCommutative, 2, 0, 1)                            \
  V(Int64Add, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)      \
  V(Int64Sub, Operator::kNoProperties, 2, 0, 1)                              \
  V(Int64Mul, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)      \
  V(Int64MulHigh, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)  \
  V(Int64Div, Operator::kNoProperties, 2, 1, 1)                              \
  V(Int64Mod, Operator::kNoProperties, 2, 1, 1)                              \
  V(Int64LessThan, Operator::kNoProperties, 2, 0, 1)                         \
  V(Int64LessThanOrEqual, Operator::kNoProperties, 2, 0, 1)                  \
  V(Uint64MulHigh, Operator::kAssociative | Operator::kCommutative, 2, 0, 1) \
  V(Uint64Div, Operator::kNoProperties, 2, 1, 1)                             \
  V(Uint64Mod, Operator::kNoProperties, 2, 1, 1)                             \
  V(Uint64LessThan, Operator::kNoProperties, 2, 0, 1)                        \
  V(Uint64LessThanOrEqual, Operator::kNoProperties, 2, 0, 1)

// The format is:
// V(Name, properties, value_input_count, control_input_count, output_count)
#define PURE_SIMD_OP_LIST(V)                                                   \
  IF_WASM(V, F64x2Splat, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, F64x2Abs, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, F64x2Neg, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, F64x2Sqrt, Operator::kNoProperties, 1, 0, 1)                      \
  IF_WASM(V, F64x2Add, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, F64x2Sub, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, F64x2Mul, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, F64x2Div, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, F64x2Min, Operator::kAssociative | Operator::kCommutative, 2, 0,  \
          1)                                                                   \
  IF_WASM(V, F64x2Max, Operator::kAssociative | Operator::kCommutative, 2, 0,  \
          1)                                                                   \
  IF_WASM(V, F64x2Eq, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, F64x2Ne, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, F64x2Lt, Operator::kNoProperties, 2, 0, 1)                        \
  IF_WASM(V, F64x2Le, Operator::kNoProperties, 2, 0, 1)                        \
  IF_WASM(V, F64x2Qfma, Operator::kNoProperties, 3, 0, 1)                      \
  IF_WASM(V, F64x2Qfms, Operator::kNoProperties, 3, 0, 1)                      \
  IF_WASM(V, F64x2Pmin, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, F64x2Pmax, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, F64x2Ceil, Operator::kNoProperties, 1, 0, 1)                      \
  IF_WASM(V, F64x2Floor, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, F64x2Trunc, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, F64x2NearestInt, Operator::kNoProperties, 1, 0, 1)                \
  IF_WASM(V, F64x2ConvertLowI32x4S, Operator::kNoProperties, 1, 0, 1)          \
  IF_WASM(V, F64x2ConvertLowI32x4U, Operator::kNoProperties, 1, 0, 1)          \
  IF_WASM(V, F64x2PromoteLowF32x4, Operator::kNoProperties, 1, 0, 1)           \
  IF_WASM(V, F32x4Splat, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, F32x4SConvertI32x4, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, F32x4UConvertI32x4, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, F32x4Abs, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, F32x4Neg, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, F32x4Sqrt, Operator::kNoProperties, 1, 0, 1)                      \
  IF_WASM(V, F32x4Add, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, F32x4Sub, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, F32x4Mul, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, F32x4Div, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, F32x4Min, Operator::kAssociative | Operator::kCommutative, 2, 0,  \
          1)                                                                   \
  IF_WASM(V, F32x4Max, Operator::kAssociative | Operator::kCommutative, 2, 0,  \
          1)                                                                   \
  IF_WASM(V, F32x4Eq, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, F32x4Ne, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, F32x4Lt, Operator::kNoProperties, 2, 0, 1)                        \
  IF_WASM(V, F32x4Le, Operator::kNoProperties, 2, 0, 1)                        \
  IF_WASM(V, F32x4Qfma, Operator::kNoProperties, 3, 0, 1)                      \
  IF_WASM(V, F32x4Qfms, Operator::kNoProperties, 3, 0, 1)                      \
  IF_WASM(V, F32x4Pmin, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, F32x4Pmax, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, F32x4Ceil, Operator::kNoProperties, 1, 0, 1)                      \
  IF_WASM(V, F32x4Floor, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, F32x4Trunc, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, F32x4NearestInt, Operator::kNoProperties, 1, 0, 1)                \
  IF_WASM(V, F32x4DemoteF64x2Zero, Operator::kNoProperties, 1, 0, 1)           \
  IF_WASM(V, F16x8Splat, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, F16x8Abs, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, F16x8Neg, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, F16x8Sqrt, Operator::kNoProperties, 1, 0, 1)                      \
  IF_WASM(V, F16x8Ceil, Operator::kNoProperties, 1, 0, 1)                      \
  IF_WASM(V, F16x8Floor, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, F16x8Trunc, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, F16x8NearestInt, Operator::kNoProperties, 1, 0, 1)                \
  IF_WASM(V, F16x8Add, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, F16x8Sub, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, F16x8Mul, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, F16x8Div, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, F16x8Min, Operator::kAssociative | Operator::kCommutative, 2, 0,  \
          1)                                                                   \
  IF_WASM(V, F16x8Max, Operator::kAssociative | Operator::kCommutative, 2, 0,  \
          1)                                                                   \
  IF_WASM(V, F16x8Pmin, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, F16x8Pmax, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, F16x8Eq, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, F16x8Ne, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, F16x8Lt, Operator::kNoProperties, 2, 0, 1)                        \
  IF_WASM(V, F16x8Le, Operator::kNoProperties, 2, 0, 1)                        \
  IF_WASM(V, F16x8SConvertI16x8, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, F16x8UConvertI16x8, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, I16x8UConvertF16x8, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, I16x8SConvertF16x8, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, F16x8DemoteF32x4Zero, Operator::kNoProperties, 1, 0, 1)           \
  IF_WASM(V, F16x8DemoteF64x2Zero, Operator::kNoProperties, 1, 0, 1)           \
  IF_WASM(V, F32x4PromoteLowF16x8, Operator::kNoProperties, 1, 0, 1)           \
  IF_WASM(V, F16x8Qfma, Operator::kNoProperties, 3, 0, 1)                      \
  IF_WASM(V, F16x8Qfms, Operator::kNoProperties, 3, 0, 1)                      \
  IF_WASM(V, I64x4Splat, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, I64x2Splat, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, I64x2SplatI32Pair, Operator::kNoProperties, 2, 0, 1)              \
  IF_WASM(V, I64x2Abs, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, I64x2Neg, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, I64x2SConvertI32x4Low, Operator::kNoProperties, 1, 0, 1)          \
  IF_WASM(V, I64x2SConvertI32x4High, Operator::kNoProperties, 1, 0, 1)         \
  IF_WASM(V, I64x2UConvertI32x4Low, Operator::kNoProperties, 1, 0, 1)          \
  IF_WASM(V, I64x2UConvertI32x4High, Operator::kNoProperties, 1, 0, 1)         \
  IF_WASM(V, I64x2BitMask, Operator::kNoProperties, 1, 0, 1)                   \
  IF_WASM(V, I64x2Shl, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I64x2ShrS, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I64x2Add, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I64x2Sub, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I64x2Mul, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I64x2Eq, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I64x2Ne, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I64x2GtS, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I64x2GeS, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I64x2ShrU, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I64x2ExtMulLowI32x4S, Operator::kCommutative, 2, 0, 1)            \
  IF_WASM(V, I64x2ExtMulHighI32x4S, Operator::kCommutative, 2, 0, 1)           \
  IF_WASM(V, I64x2ExtMulLowI32x4U, Operator::kCommutative, 2, 0, 1)            \
  IF_WASM(V, I64x2ExtMulHighI32x4U, Operator::kCommutative, 2, 0, 1)           \
  IF_WASM(V, I32x8Splat, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, I32x4Splat, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, I32x4SConvertF32x4, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, I32x4SConvertI16x8Low, Operator::kNoProperties, 1, 0, 1)          \
  IF_WASM(V, I32x4SConvertI16x8High, Operator::kNoProperties, 1, 0, 1)         \
  IF_WASM(V, I32x4Neg, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, I32x4Shl, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I32x4ShrS, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I32x4Add, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I32x4Sub, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I32x4Mul, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I32x4MinS, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, I32x4MaxS, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, I32x4Eq, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I32x4Ne, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I32x4GtS, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I32x4GeS, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I32x4UConvertF32x4, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, I32x4UConvertI16x8Low, Operator::kNoProperties, 1, 0, 1)          \
  IF_WASM(V, I32x4UConvertI16x8High, Operator::kNoProperties, 1, 0, 1)         \
  IF_WASM(V, I32x4ShrU, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I32x4MinU, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, I32x4MaxU, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, I32x4GtU, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I32x4GeU, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I32x4Abs, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, I32x4BitMask, Operator::kNoProperties, 1, 0, 1)                   \
  IF_WASM(V, I32x4DotI16x8S, Operator::kCommutative, 2, 0, 1)                  \
  IF_WASM(V, I32x4ExtMulLowI16x8S, Operator::kCommutative, 2, 0, 1)            \
  IF_WASM(V, I32x4ExtMulHighI16x8S, Operator::kCommutative, 2, 0, 1)           \
  IF_WASM(V, I32x4ExtMulLowI16x8U, Operator::kCommutative, 2, 0, 1)            \
  IF_WASM(V, I32x4ExtMulHighI16x8U, Operator::kCommutative, 2, 0, 1)           \
  IF_WASM(V, I32x4ExtAddPairwiseI16x8S, Operator::kNoProperties, 1, 0, 1)      \
  IF_WASM(V, I32x4ExtAddPairwiseI16x8U, Operator::kNoProperties, 1, 0, 1)      \
  IF_WASM(V, I32x4TruncSatF64x2SZero, Operator::kNoProperties, 1, 0, 1)        \
  IF_WASM(V, I32x4TruncSatF64x2UZero, Operator::kNoProperties, 1, 0, 1)        \
  IF_WASM(V, I16x16Splat, Operator::kNoProperties, 1, 0, 1)                    \
  IF_WASM(V, I16x8Splat, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, I16x8SConvertI8x16Low, Operator::kNoProperties, 1, 0, 1)          \
  IF_WASM(V, I16x8SConvertI8x16High, Operator::kNoProperties, 1, 0, 1)         \
  IF_WASM(V, I16x8Neg, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, I16x8Shl, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I16x8ShrS, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I16x8SConvertI32x4, Operator::kNoProperties, 2, 0, 1)             \
  IF_WASM(V, I16x8Add, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I16x8AddSatS, Operator::kCommutative, 2, 0, 1)                    \
  IF_WASM(V, I16x8Sub, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I16x8SubSatS, Operator::kNoProperties, 2, 0, 1)                   \
  IF_WASM(V, I16x8Mul, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I16x8MinS, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, I16x8MaxS, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, I16x8Eq, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I16x8Ne, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I16x8GtS, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I16x8GeS, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I16x8UConvertI8x16Low, Operator::kNoProperties, 1, 0, 1)          \
  IF_WASM(V, I16x8UConvertI8x16High, Operator::kNoProperties, 1, 0, 1)         \
  IF_WASM(V, I16x8ShrU, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I16x8UConvertI32x4, Operator::kNoProperties, 2, 0, 1)             \
  IF_WASM(V, I16x8AddSatU, Operator::kCommutative, 2, 0, 1)                    \
  IF_WASM(V, I16x8SubSatU, Operator::kNoProperties, 2, 0, 1)                   \
  IF_WASM(V, I16x8MinU, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, I16x8MaxU, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, I16x8GtU, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I16x8GeU, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I16x8RoundingAverageU, Operator::kCommutative, 2, 0, 1)           \
  IF_WASM(V, I16x8Q15MulRSatS, Operator::kCommutative, 2, 0, 1)                \
  IF_WASM(V, I16x8Abs, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, I16x8BitMask, Operator::kNoProperties, 1, 0, 1)                   \
  IF_WASM(V, I16x8ExtMulLowI8x16S, Operator::kCommutative, 2, 0, 1)            \
  IF_WASM(V, I16x8ExtMulHighI8x16S, Operator::kCommutative, 2, 0, 1)           \
  IF_WASM(V, I16x8ExtMulLowI8x16U, Operator::kCommutative, 2, 0, 1)            \
  IF_WASM(V, I16x8ExtMulHighI8x16U, Operator::kCommutative, 2, 0, 1)           \
  IF_WASM(V, I16x8ExtAddPairwiseI8x16S, Operator::kNoProperties, 1, 0, 1)      \
  IF_WASM(V, I16x8ExtAddPairwiseI8x16U, Operator::kNoProperties, 1, 0, 1)      \
  IF_WASM(V, I8x32Splat, Operator::kNoProperties, 1, 0, 1)                     \
  V(I8x16Splat, Operator::kNoProperties, 1, 0, 1)                              \
  IF_WASM(V, F64x4Splat, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, F32x8Splat, Operator::kNoProperties, 1, 0, 1)                     \
  IF_WASM(V, I8x16Neg, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, I8x16Shl, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I8x16ShrS, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I8x16SConvertI16x8, Operator::kNoProperties, 2, 0, 1)             \
  IF_WASM(V, I8x16Add, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I8x16AddSatS, Operator::kCommutative, 2, 0, 1)                    \
  IF_WASM(V, I8x16Sub, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I8x16SubSatS, Operator::kNoProperties, 2, 0, 1)                   \
  IF_WASM(V, I8x16MinS, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, I8x16MaxS, Operator::kCommutative, 2, 0, 1)                       \
  V(I8x16Eq, Operator::kCommutative, 2, 0, 1)                                  \
  IF_WASM(V, I8x16Ne, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I8x16GtS, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I8x16GeS, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I8x16ShrU, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I8x16UConvertI16x8, Operator::kNoProperties, 2, 0, 1)             \
  IF_WASM(V, I8x16AddSatU, Operator::kCommutative, 2, 0, 1)                    \
  IF_WASM(V, I8x16SubSatU, Operator::kNoProperties, 2, 0, 1)                   \
  IF_WASM(V, I8x16MinU, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, I8x16MaxU, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, I8x16GtU, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I8x16GeU, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I8x16RoundingAverageU, Operator::kCommutative, 2, 0, 1)           \
  IF_WASM(V, I8x16Popcnt, Operator::kNoProperties, 1, 0, 1)                    \
  IF_WASM(V, I8x16Abs, Operator::kNoProperties, 1, 0, 1)                       \
  V(I8x16BitMask, Operator::kNoProperties, 1, 0, 1)                            \
  IF_WASM(V, S128Zero, Operator::kNoProperties, 0, 0, 1)                       \
  IF_WASM(V, S128And, Operator::kAssociative | Operator::kCommutative, 2, 0,   \
          1)                                                                   \
  IF_WASM(V, S128Or, Operator::kAssociative | Operator::kCommutative, 2, 0, 1) \
  IF_WASM(V, S128Xor, Operator::kAssociative | Operator::kCommutative, 2, 0,   \
          1)                                                                   \
  IF_WASM(V, S128Not, Operator::kNoProperties, 1, 0, 1)                        \
  IF_WASM(V, S128Select, Operator::kNoProperties, 3, 0, 1)                     \
  IF_WASM(V, S128AndNot, Operator::kNoProperties, 2, 0, 1)                     \
  IF_WASM(V, V128AnyTrue, Operator::kNoProperties, 1, 0, 1)                    \
  IF_WASM(V, I64x2AllTrue, Operator::kNoProperties, 1, 0, 1)                   \
  IF_WASM(V, I32x4AllTrue, Operator::kNoProperties, 1, 0, 1)                   \
  IF_WASM(V, I16x8AllTrue, Operator::kNoProperties, 1, 0, 1)                   \
  IF_WASM(V, I8x16AllTrue, Operator::kNoProperties, 1, 0, 1)                   \
  IF_WASM(V, I8x16RelaxedLaneSelect, Operator::kNoProperties, 3, 0, 1)         \
  IF_WASM(V, I16x8RelaxedLaneSelect, Operator::kNoProperties, 3, 0, 1)         \
  IF_WASM(V, I32x4RelaxedLaneSelect, Operator::kNoProperties, 3, 0, 1)         \
  IF_WASM(V, I64x2RelaxedLaneSelect, Operator::kNoProperties, 3, 0, 1)         \
  IF_WASM(V, F32x4RelaxedMin, Operator::kNoProperties, 2, 0, 1)                \
  IF_WASM(V, F32x4RelaxedMax, Operator::kNoProperties, 2, 0, 1)                \
  IF_WASM(V, F64x2RelaxedMin, Operator::kNoProperties, 2, 0, 1)                \
  IF_WASM(V, F64x2RelaxedMax, Operator::kNoProperties, 2, 0, 1)                \
  IF_WASM(V, F32x8RelaxedMin, Operator::kNoProperties, 2, 0, 1)                \
  IF_WASM(V, F32x8RelaxedMax, Operator::kNoProperties, 2, 0, 1)                \
  IF_WASM(V, F64x4RelaxedMin, Operator::kNoProperties, 2, 0, 1)                \
  IF_WASM(V, F64x4RelaxedMax, Operator::kNoProperties, 2, 0, 1)                \
  IF_WASM(V, I32x4RelaxedTruncF32x4S, Operator::kNoProperties, 1, 0, 1)        \
  IF_WASM(V, I32x4RelaxedTruncF32x4U, Operator::kNoProperties, 1, 0, 1)        \
  IF_WASM(V, I32x4RelaxedTruncF64x2SZero, Operator::kNoProperties, 1, 0, 1)    \
  IF_WASM(V, I32x4RelaxedTruncF64x2UZero, Operator::kNoProperties, 1, 0, 1)    \
  IF_WASM(V, I16x8RelaxedQ15MulRS, Operator::kCommutative, 2, 0, 1)            \
  IF_WASM(V, I16x8DotI8x16I7x16S, Operator::kNoProperties, 2, 0, 1)            \
  IF_WASM(V, I32x4DotI8x16I7x16AddS, Operator::kNoProperties, 3, 0, 1)         \
  IF_WASM(V, F64x4Min, Operator::kAssociative | Operator::kCommutative, 2, 0,  \
          1)                                                                   \
  IF_WASM(V, F64x4Max, Operator::kAssociative | Operator::kCommutative, 2, 0,  \
          1)                                                                   \
  IF_WASM(V, F64x4Add, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, F64x4Sqrt, Operator::kNoProperties, 1, 0, 1)                      \
  IF_WASM(V, F32x8Abs, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, F32x8Neg, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, F32x8Sqrt, Operator::kNoProperties, 1, 0, 1)                      \
  IF_WASM(V, F32x8Add, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I64x4Add, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I32x8Add, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I16x16Add, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, I8x32Add, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, F64x4Sub, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, F32x8Sub, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I64x4Sub, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I32x8Sub, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I16x16Sub, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I8x32Sub, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, F64x4Mul, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, F32x8Mul, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I64x4Mul, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I32x8Mul, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I16x16Mul, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, F64x4Div, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, F32x8Div, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I16x16AddSatS, Operator::kCommutative, 2, 0, 1)                   \
  IF_WASM(V, I8x32AddSatS, Operator::kCommutative, 2, 0, 1)                    \
  IF_WASM(V, I16x16AddSatU, Operator::kCommutative, 2, 0, 1)                   \
  IF_WASM(V, I8x32AddSatU, Operator::kCommutative, 2, 0, 1)                    \
  IF_WASM(V, I16x16SubSatS, Operator::kNoProperties, 2, 0, 1)                  \
  IF_WASM(V, I8x32SubSatS, Operator::kNoProperties, 2, 0, 1)                   \
  IF_WASM(V, I16x16SubSatU, Operator::kNoProperties, 2, 0, 1)                  \
  IF_WASM(V, I8x32SubSatU, Operator::kNoProperties, 2, 0, 1)                   \
  IF_WASM(V, F32x8Min, Operator::kAssociative | Operator::kCommutative, 2, 0,  \
          1)                                                                   \
  IF_WASM(V, F32x8Max, Operator::kAssociative | Operator::kCommutative, 2, 0,  \
          1)                                                                   \
  IF_WASM(V, F32x8Pmin, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, F32x8Pmax, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, F32x8Eq, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, F64x4Eq, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I64x4Eq, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I32x8Eq, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I16x16Eq, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I8x32Eq, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, F32x8Ne, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, F64x4Ne, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I64x4GtS, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I32x8GtS, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I16x16GtS, Operator::kCommutative, 2, 0, 1)                       \
  IF_WASM(V, I8x32GtS, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, F64x4Lt, Operator::kNoProperties, 2, 0, 1)                        \
  IF_WASM(V, F32x8Lt, Operator::kNoProperties, 2, 0, 1)                        \
  IF_WASM(V, F64x4Le, Operator::kNoProperties, 2, 0, 1)                        \
  IF_WASM(V, F32x8Le, Operator::kNoProperties, 2, 0, 1)                        \
  IF_WASM(V, I32x8MinS, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I16x16MinS, Operator::kNoProperties, 2, 0, 1)                     \
  IF_WASM(V, I8x32MinS, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I32x8MinU, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I16x16MinU, Operator::kNoProperties, 2, 0, 1)                     \
  IF_WASM(V, I8x32MinU, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I32x8MaxS, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I16x16MaxS, Operator::kNoProperties, 2, 0, 1)                     \
  IF_WASM(V, I8x32MaxS, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I32x8MaxU, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I16x16MaxU, Operator::kNoProperties, 2, 0, 1)                     \
  IF_WASM(V, I8x32MaxU, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I64x4Ne, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I64x4GeS, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I32x8Ne, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I32x8GtU, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I32x8GeS, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I32x8GeU, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I16x16Ne, Operator::kCommutative, 2, 0, 1)                        \
  IF_WASM(V, I16x16GtU, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I16x16GeS, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I16x16GeU, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I8x32Ne, Operator::kCommutative, 2, 0, 1)                         \
  IF_WASM(V, I8x32GtU, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I8x32GeS, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I8x32GeU, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I32x8SConvertF32x8, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, I32x8UConvertF32x8, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, F64x4ConvertI32x4S, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, F32x8SConvertI32x8, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, F32x8UConvertI32x8, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, F32x4DemoteF64x4, Operator::kNoProperties, 1, 0, 1)               \
  IF_WASM(V, I64x4SConvertI32x4, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, I64x4UConvertI32x4, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, I32x8SConvertI16x8, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, I32x8UConvertI16x8, Operator::kNoProperties, 1, 0, 1)             \
  IF_WASM(V, I16x16SConvertI8x16, Operator::kNoProperties, 1, 0, 1)            \
  IF_WASM(V, I16x16UConvertI8x16, Operator::kNoProperties, 1, 0, 1)            \
  IF_WASM(V, I16x16SConvertI32x8, Operator::kNoProperties, 2, 0, 1)            \
  IF_WASM(V, I16x16UConvertI32x8, Operator::kNoProperties, 2, 0, 1)            \
  IF_WASM(V, I8x32SConvertI16x16, Operator::kNoProperties, 2, 0, 1)            \
  IF_WASM(V, I8x32UConvertI16x16, Operator::kNoProperties, 2, 0, 1)            \
  IF_WASM(V, I32x8Neg, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, I32x8Abs, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, I16x16Neg, Operator::kNoProperties, 1, 0, 1)                      \
  IF_WASM(V, I16x16Abs, Operator::kNoProperties, 1, 0, 1)                      \
  IF_WASM(V, I8x32Neg, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, I8x32Abs, Operator::kNoProperties, 1, 0, 1)                       \
  IF_WASM(V, I64x4Shl, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I64x4ShrU, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I32x8Shl, Operator::kNoProperties, 2, 0, 1)                       \
  IF_WASM(V, I32x8ShrS, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I32x8ShrU, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I16x16Shl, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, I16x16ShrS, Operator::kNoProperties, 2, 0, 1)                     \
  IF_WASM(V, I16x16ShrU, Operator::kNoProperties, 2, 0, 1)                     \
  IF_WASM(V, I32x8DotI16x16S, Operator::kCommutative, 2, 0, 1)                 \
  IF_WASM(V, I16x16RoundingAverageU, Operator::kCommutative, 2, 0, 1)          \
  IF_WASM(V, I8x32RoundingAverageU, Operator::kCommutative, 2, 0, 1)           \
  IF_WASM(V, I64x4ExtMulI32x4S, Operator::kCommutative, 2, 0, 1)               \
  IF_WASM(V, I64x4ExtMulI32x4U, Operator::kCommutative, 2, 0, 1)               \
  IF_WASM(V, I32x8ExtMulI16x8S, Operator::kCommutative, 2, 0, 1)               \
  IF_WASM(V, I32x8ExtMulI16x8U, Operator::kCommutative, 2, 0, 1)               \
  IF_WASM(V, I16x16ExtMulI8x16S, Operator::kCommutative, 2, 0, 1)              \
  IF_WASM(V, I16x16ExtMulI8x16U, Operator::kCommutative, 2, 0, 1)              \
  IF_WASM(V, I32x8ExtAddPairwiseI16x16S, Operator::kNoProperties, 1, 0, 1)     \
  IF_WASM(V, I32x8ExtAddPairwiseI16x16U, Operator::kNoProperties, 1, 0, 1)     \
  IF_WASM(V, I16x16ExtAddPairwiseI8x32S, Operator::kNoProperties, 1, 0, 1)     \
  IF_WASM(V, I16x16ExtAddPairwiseI8x32U, Operator::kNoProperties, 1, 0, 1)     \
  IF_WASM(V, F64x4Pmin, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, F64x4Pmax, Operator::kNoProperties, 2, 0, 1)                      \
  IF_WASM(V, S256Zero, Operator::kNoProperties, 0, 0, 1)                       \
  IF_WASM(V, S256And, Operator::kAssociative | Operator::kCommutative, 2, 0,   \
          1)                                                                   \
  IF_WASM(V, S256Or, Operator::kAssociative | Operator::kCommutative, 2, 0, 1) \
  IF_WASM(V, S256Xor, Operator::kAssociative | Operator::kCommutative, 2, 0,   \
          1)                                                                   \
  IF_WASM(V, S256Not, Operator::kNoProperties, 1, 0, 1)                        \
  IF_WASM(V, S256Select, Operator::kNoProperties, 3, 0, 1)                     \
  IF_WASM(V, S256AndNot, Operator::kNoProperties, 2, 0, 1)                     \
  IF_WASM(V, F32x8Qfma, Operator::kNoProperties, 3, 0, 1)                      \
  IF_WASM(V, F32x8Qfms, Operator::kNoProperties, 3, 0, 1)                      \
  IF_WASM(V, F64x4Qfma, Operator::kNoProperties, 3, 0, 1)                      \
  IF_WASM(V, F64x4Qfms, Operator::kNoProperties, 3, 0, 1)                      \
  IF_WASM(V, I64x4RelaxedLaneSelect, Operator::kNoProperties, 3, 0, 1)         \
  IF_WASM(V, I32x8RelaxedLaneSelect, Operator::kNoProperties, 3, 0, 1)         \
  IF_WASM(V, I16x16RelaxedLaneSelect, Operator::kNoProperties, 3, 0, 1)        \
  IF_WASM(V, I8x32RelaxedLaneSelect, Operator::kNoProperties, 3, 0, 1)         \
  IF_WASM(V, I32x8DotI8x32I7x32AddS, Operator::kNoProperties, 3, 0, 1)         \
  IF_WASM(V, I16x16DotI8x32I7x32S, Operator::kNoProperties, 2, 0, 1)

// The format is:
// V(Name, properties, value_input_count, control_input_count, output_count)
#define MACHINE_PURE_OP_LIST(V)                                            \
  PURE_BINARY_OP_LIST_32(V)                                                \
  PURE_BINARY_OP_LIST_64(V)                                                \
  PURE_SIMD_OP_LIST(V)                                                     \
  V(Word32Clz, Operator::kNoProperties, 1, 0, 1)                           \
  V(Word64Clz, Operator::kNoProperties, 1, 0, 1)                           \
  V(Word64ClzLowerable, Operator::kNoProperties, 1, 1, 1)                  \
  V(Word32ReverseBytes, Operator::kNoProperties, 1, 0, 1)                  \
  V(Word64ReverseBytes, Operator::kNoProperties, 1, 0, 1)                  \
  V(Simd128ReverseBytes, Operator::kNoProperties, 1, 0, 1)                 \
  V(BitcastTaggedToWordForTagAndSmiBits, Operator::kNoProperties, 1, 0, 1) \
  V(BitcastWordToTaggedSigned, Operator::kNoProperties, 1, 0, 1)           \
  V(TruncateFloat64ToWord32, Operator::kNoProperties, 1, 0, 1)             \
  V(ChangeFloat32ToFloat64, Operator::kNoProperties, 1, 0, 1)              \
  V(ChangeFloat64ToInt32, Operator::kNoProperties, 1, 0, 1)                \
  V(ChangeFloat64ToInt64, Operator::kNoProperties, 1, 0, 1)                \
  V(ChangeFloat64ToUint32, Operator::kNoProperties, 1, 0, 1)               \
  V(ChangeFloat64ToUint64, Operator::kNoProperties, 1, 0, 1)               \
  V(TruncateFloat64ToUint32, Operator::kNoProperties, 1, 0, 1)             \
  V(TryTruncateFloat32ToInt64, Operator::kNoProperties, 1, 0, 2)           \
  V(TryTruncateFloat64ToInt64, Operator::kNoProperties, 1, 0, 2)           \
  V(TryTruncateFloat32ToUint64, Operator::kNoProperties, 1, 0, 2)          \
  V(TryTruncateFloat64ToUint64, Operator::kNoProperties, 1, 0, 2)          \
  V(TryTruncateFloat64ToInt32, Operator::kNoProperties, 1, 0, 2)           \
  V(TryTruncateFloat64ToUint32, Operator::kNoProperties, 1, 0, 2)          \
  V(ChangeInt32ToFloat64, Operator::kNoProperties, 1, 0, 1)                \
  V(ChangeInt64ToFloat64, Operator::kNoProperties, 1, 0, 1)                \
  V(Float64SilenceNaN, Operator::kNoProperties, 1, 0, 1)                   \
  V(RoundFloat64ToInt32, Operator::kNoProperties, 1, 0, 1)                 \
  V(RoundInt32ToFloat32, Operator::kNoProperties, 1, 0, 1)                 \
  V(RoundInt64ToFloat32, Operator::kNoProperties, 1, 0, 1)                 \
  V(RoundInt64ToFloat64, Operator::kNoProperties, 1, 0, 1)                 \
  V(RoundUint32ToFloat32, Operator::kNoProperties, 1, 0, 1)                \
  V(RoundUint64ToFloat32, Operator::kNoProperties, 1, 0, 1)                \
  V(RoundUint64ToFloat64, Operator::kNoProperties, 1, 0, 1)                \
  V(BitcastWord32ToWord64, Operator::kNoProperties, 1, 0, 1)               \
  V(ChangeInt32ToInt64, Operator::kNoProperties, 1, 0, 1)                  \
  V(ChangeUint32ToFloat64, Operator::kNoProperties, 1, 0, 1)               \
  V(ChangeUint32ToUint64, Operator::kNoProperties, 1, 0, 1)                \
  V(TruncateFloat64ToFloat32, Operator::kNoProperties, 1, 0, 1)            \
  V(TruncateInt64ToInt32, Operator::kNoProperties, 1, 0, 1)                \
  V(BitcastFloat32ToInt32, Operator::kNoProperties, 1, 0, 1)               \
  V(BitcastFloat64ToInt64, Operator::kNoProperties, 1, 0, 1)               \
  V(BitcastInt32ToFloat32, Operator::kNoProperties, 1, 0, 1)               \
  V(BitcastInt64ToFloat64, Operator::kNoProperties, 1, 0, 1)               \
  V(SignExtendWord8ToInt32, Operator::kNoProperties, 1, 0, 1)              \
  V(SignExtendWord16ToInt32, Operator::kNoProperties, 1, 0, 1)             \
  V(SignExtendWord8ToInt64, Operator::kNoProperties, 1, 0, 1)              \
  V(SignExtendWord16ToInt64, Operator::kNoProperties, 1, 0, 1)             \
  V(SignExtendWord32ToInt64, Operator::kNoProperties, 1, 0, 1)             \
  V(Float32Abs, Operator::kNoProperties, 1, 0, 1)                          \
  V(Float32Add, Operator::kCommutative, 2, 0, 1)                           \
  V(Float32Sub, Operator::kNoProperties, 2, 0, 1)                          \
  V(Float32Mul, Operator::kCommutative, 2, 0, 1)                           \
  V(Float32Div, Operator::kNoProperties, 2, 0, 1)                          \
  V(Float32Neg, Operator::kNoProperties, 1, 0, 1)                          \
  V(Float32Sqrt, Operator::kNoProperties, 1, 0, 1)                         \
  V(Float32Max, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)  \
  V(Float32Min, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)  \
  V(Float64Abs, Operator::kNoProperties, 1, 0, 1)                          \
  V(Float64Acos, Operator::kNoProperties, 1, 0, 1)                         \
  V(Float64Acosh, Operator::kNoProperties, 1, 0, 1)                        \
  V(Float64Asin, Operator::kNoProperties, 1, 0, 1)                         \
  V(Float64Asinh, Operator::kNoProperties, 1, 0, 1)                        \
  V(Float64Atan, Operator::kNoProperties, 1, 0, 1)                         \
  V(Float64Atan2, Operator::kNoProperties, 2, 0, 1)                        \
  V(Float64Atanh, Operator::kNoProperties, 1, 0, 1)                        \
  V(Float64Cbrt, Operator::kNoProperties, 1, 0, 1)                         \
  V(Float64Cos, Operator::kNoProperties, 1, 0, 1)                          \
  V(Float64Cosh, Operator::kNoProperties, 1, 0, 1)                         \
  V(Float64Exp, Operator::kNoProperties, 1, 0, 1)                          \
  V(Float64Expm1, Operator::kNoProperties, 1, 0, 1)                        \
  V(Float64Log, Operator::kNoProperties, 1, 0, 1)                          \
  V(Float64Log1p, Operator::kNoProperties, 1, 0, 1)                        \
  V(Float64Log2, Operator::kNoProperties, 1, 0, 1)                         \
  V(Float64Log10, Operator::kNoProperties, 1, 0, 1)                        \
  V(Float64Max, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)  \
  V(Float64Min, Operator::kAssociative | Operator::kCommutative, 2, 0, 1)  \
  V(Float64Neg, Operator::kNoProperties, 1, 0, 1)                          \
  V(Float64Add, Operator::kCommutative, 2, 0, 1)                           \
  V(Float64Sub, Operator::kNoProperties, 2, 0, 1)                          \
  V(Float64Mul, Operator::kCommutative, 2, 0, 1)                           \
  V(Float64Div, Operator::kNoProperties, 2, 0, 1)                          \
  V(Float64Mod, Operator::kNoProperties, 2, 0, 1)                          \
  V(Float64Pow, Operator::kNoProperties, 2, 0, 1)                          \
  V(Float64Sin, Operator::kNoProperties, 1, 0, 1)                          \
  V(Float64Sinh, Operator::kNoProperties, 1, 0, 1)                         \
  V(Float64Sqrt, Operator::kNoProperties, 1, 0, 1)                         \
  V(Float64Tan, Operator::kNoProperties, 1, 0, 1)                          \
  V(Float64Tanh, Operator::kNoProperties, 1, 0, 1)                         \
  V(Float32Equal, Operator::kCommutative, 2, 0, 1)                         \
  V(Float32LessThan, Operator::kNoProperties, 2, 0, 1)                     \
  V(Float32LessThanOrEqual, Operator::kNoProperties, 2, 0, 1)              \
  V(Float64Equal, Operator::kCommutative, 2, 0, 1)                         \
  V(Float64LessThan, Operator::kNoProperties, 2, 0, 1)                     \
  V(Float64LessThanOrEqual, Operator::kNoProperties, 2, 0, 1)              \
  V(Float64ExtractLowWord32, Operator::kNoProperties, 1, 0, 1)             \
  V(Float64ExtractHighWord32, Operator::kNoProperties, 1, 0, 1)            \
  V(Float64InsertLowWord32, Operator::kNoProperties, 2, 0, 1)              \
  V(Float64InsertHighWord32, Operator::kNoProperties, 2, 0, 1)             \
  V(LoadStackCheckOffset, Operator::kNoProperties, 0, 0, 1)                \
  V(LoadFramePointer, Operator::kNoProperties, 0, 0, 1)                    \
  V(LoadRootRegister, Operator::kNoProperties, 0, 0, 1)                    \
  V(LoadParentFramePointer, Operator::kNoProperties, 0, 0, 1)              \
  V(Int32PairAdd, Operator::kNoProperties, 4, 0, 2)                        \
  V(Int32PairSub, Operator::kNoProperties, 4, 0, 2)                        \
  V(Int32PairMul, Operator::kNoProperties, 4, 0, 2)                        \
  V(Word32PairShl, Operator::kNoProperties, 3, 0, 2)                       \
  V(Word32PairShr, Operator::kNoProperties, 3, 0, 2)                       \
  V(Word32PairSar, Operator::kNoProperties, 3, 0, 2)

// The format is:
// V(Name, properties, value_input_count, control_input_count, output_count)
#define PURE_OPTIONAL_OP_LIST(V)                            \
  V(Word32Ctz, Operator::kNoProperties, 1, 0, 1)            \
  V(Word64Ctz, Operator::kNoProperties, 1, 0, 1)            \
  V(Word64CtzLowerable, Operator::kNoProperties, 1, 1, 1)   \
  V(Word32Rol, Operator::kNoProperties, 2, 0, 1)            \
  V(Word64Rol, Operator::kNoProperties, 2, 0, 1)            \
  V(Word64RolLowerable, Operator::kNoProperties, 2, 1, 1)   \
  V(Word32ReverseBits, Operator::kNoProperties, 1, 0, 1)    \
  V(Word64ReverseBits, Operator::kNoProperties, 1, 0, 1)    \
  V(Int32AbsWithOverflow, Operator::kNoProperties, 1, 0, 2) \
  V(Int64AbsWithOverflow, Operator::kNoProperties, 1, 0, 2) \
  V(Word32Popcnt, Operator::kNoProperties, 1, 0, 1)         \
  V(Word64Popcnt, Operator::kNoProperties, 1, 0, 1)         \
  V(Float32RoundDown, Operator::kNoProperties, 1, 0, 1)     \
  V(Float64RoundDown, Operator::kNoProperties, 1, 0, 1)     \
  V(Float32RoundUp, Operator::kNoProperties, 1, 0, 1)       \
  V(Float64RoundUp, Operator::kNoProperties, 1, 0, 1)       \
  V(Float32RoundTruncate, Operator::kNoProperties, 1, 0, 1) \
  V(Float64RoundTruncate, Operator::kNoProperties, 1, 0, 1) \
  V(Float64RoundTiesAway, Operator::kNoProperties, 1, 0, 1) \
  V(Float32RoundTiesEven, Operator::kNoProperties, 1, 0, 1) \
  V(Float64RoundTiesEven, Operator::kNoProperties, 1, 0, 1) \
  V(Word32Select, Operator::kNoProperties, 3, 0, 1)         \
  V(Word64Select, Operator::kNoProperties, 3, 0, 1)         \
  V(Float32Select, Operator::kNoProperties, 3, 0, 1)        \
  V(Float64Select, Operator::kNoProperties, 3, 0, 1)

// The format is:
// V(Name, properties, value_input_count, control_input_count, output_count)
#define OVERFLOW_OP_LIST(V)                                                \
  V(Int32AddWithOverflow, Operator::kAssociative | Operator::kCommutative) \
  V(Int32SubWithOverflow, Operator::kNoProperties)                         \
  V(Int32MulWithOverflow, Operator::kAssociative | Operator::kCommutative) \
  V(Int64AddWithOverflow, Operator::kAssociative | Operator::kCommutative) \
  V(Int64SubWithOverflow, Operator::kNoProperties)                         \
  V(Int64MulWithOverflow, Operator::kAssociative | Operator::kCommutative)

#define MACHINE_TYPE_LIST(V) \
  V(Float16)                 \
  V(Float32)                 \
  V(Float64)                 \
  V(Simd128)                 \
  V(Int8)                    \
  V(Uint8)                   \
  V(Int16)                   \
  V(Uint16)                  \
  V(Int32)                   \
  V(Uint32)                  \
  V(Int64)                   \
  V(Uint64)                  \
  V(Pointer)                 \
  V(TaggedSigned)            \
  V(TaggedPointer)           \
  V(MapInHeader)             \
  V(AnyTagged)               \
  V(CompressedPointer)       \
  V(ProtectedPointer)        \
  V(SandboxedPointer)        \
  V(AnyCompressed)           \
  V(Simd256)

#define MACHINE_REPRESENTATION_LIST(V) \
  V(kFloat16)                          \
  V(kFloat32)                          \
  V(kFloat64)                          \
  V(kSimd128)                          \
  V(kWord8)                            \
  V(kWord16)                           \
  V(kWord32)                           \
  V(kWord64)                           \
  V(kMapWord)                          \
  V(kTaggedSigned)                     \
  V(kTaggedPointer)                    \
  V(kTagged)                           \
  V(kCompressedPointer)                \
  V(kSandboxedPointer)                 \
  V(kCompressed)                       \
  V(kSimd256)

#ifdef V8_TARGET_ARCH_64_BIT

#ifdef V8_COMPRESS_POINTERS

#define STORE_PAIR_MACHINE_REPRESENTATION_LIST(V) \
  V(kWord32, kWord32)                             \
  V(kWord32, kTagged)                             \
  V(kWord32, kTaggedSigned)                       \
  V(kWord32, kTaggedPointer)                      \
  V(kWord32, kCompressed)                         \
  V(kWord32, kCompressedPointer)                  \
  V(kTagged, kWord32)                             \
  V(kTagged, kTagged)                             \
  V(kTagged, kTaggedSigned)                       \
  V(kTagged, kTaggedPointer)                      \
  V(kTagged, kCompressed)                         \
  V(kTagged, kCompressedPointer)                  \
  V(kTaggedSigned, kWord32)                       \
  V(kTaggedSigned, kTagged)                       \
  V(kTaggedSigned, kTaggedSigned)                 \
  V(kTaggedSigned, kTaggedPointer)                \
  V(kTaggedSigned, kCompressed)                   \
  V(kTaggedSigned, kCompressedPointer)            \
  V(kTaggedPointer, kWord32)                      \
  V(kTaggedPointer, kTagged)                      \
  V(kTaggedPointer, kTaggedSigned)                \
  V(kTaggedPointer, kTaggedPointer)               \
  V(kTaggedPointer, kCompressed)                  \
  V(kTaggedPointer, kCompressedPointer)           \
  V(kCompressed, kWord32)                         \
  V(kCompressed, kTagged)                         \
  V(kCompressed, kTaggedSigned)                   \
  V(kCompressed, kTaggedPointer)                  \
  V(kCompressed, kCompressed)                     \
  V(kCompressed, kCompressedPointer)              \
  V(kCompressedPointer, kWord32)                  \
  V(kCompressedPointer, kTagged)                  \
  V(kCompressedPointer, kTaggedSigned)            \
  V(kCompressedPointer, kTaggedPointer)           \
  V(kCompressedPointer, kCompressed)              \
  V(kCompressedPointer, kCompressedPointer)       \
  V(kWord64, kWord64)

#else

#define STORE_PAIR_MACHINE_REPRESENTATION_LIST(V) \
  V(kWord32, kWord32)                             \
  V(kWord64, kWord64)                             \
  V(kWord64, kTagged)                             \
  V(kWord64, kTaggedSigned)                       \
  V(kWord64, kTaggedPointer)                      \
  V(kTagged, kWord64)                             \
  V(kTagged, kTagged)                             \
  V(kTagged, kTaggedSigned)                       \
  V(kTagged, kTaggedPointer)                      \
  V(kTaggedSigned, kWord64)                       \
  V(kTaggedSigned, kTagged)                       \
  V(kTaggedSigned, kTaggedSigned)                 \
  V(kTaggedSigned, kTaggedPointer)                \
  V(kTaggedPointer, kWord64)                      \
  V(kTaggedPointer, kTagged)                      \
  V(kTaggedPointer, kTaggedSigned)                \
  V(kTaggedPointer, kTaggedPointer)

#endif  // V8_COMPRESS_POINTERS

#else

#define STORE_PAIR_MACHINE_REPRESENTATION_LIST(V)

#endif  // V8_TARGET_ARCH_64_BIT

#define LOAD_TRANSFORM_LIST(V) \
  V(S128Load8Splat)            \
  V(S128Load16Splat)           \
  V(S128Load32Splat)           \
  V(S128Load64Splat)           \
  V(S128Load8x8S)              \
  V(S128Load8x8U)              \
  V(S128Load16x4S)             \
  V(S128Load16x4U)             \
  V(S128Load32x2S)             \
  V(S128Load32x2U)             \
  V(S128Load32Zero)            \
  V(S128Load64Zero)            \
  V(S256Load8Splat)            \
  V(S256Load16Splat)           \
  V(S256Load32Splat)           \
  V(S256Load64Splat)           \
  V(S256Load8x16S)             \
  V(S256Load8x16U)             \
  V(S256Load16x8S)             \
  V(S256Load16x8U)             \
  V(S256Load32x4S)             \
  V(S256Load32x4U)

#if TAGGED_SIZE_8_BYTES

#define ATOMIC_TAGGED_TYPE_LIST(V)

#define ATOMIC64_TAGGED_TYPE_LIST(V) \
  V(TaggedSigned)                    \
  V(TaggedPointer)                   \
  V(AnyTagged)                       \
  V(CompressedPointer)               \
  V(AnyCompressed)

#else

#define ATOMIC_TAGGED_TYPE_LIST(V) \
  V(TaggedSigned)                  \
  V(TaggedPointer)                 \
  V(AnyTagged)                     \
  V(CompressedPointer)             \
  V(AnyCompressed)

#define ATOMIC64_TAGGED_TYPE_LIST(V)

#endif  // TAGGED_SIZE_8_BYTES

#define ATOMIC_U32_TYPE_LIST(V) \
  V(Uint8)                      \
  V(Uint16)                     \
  V(Uint32)

#define ATOMIC_TYPE_LIST(V) \
  ATOMIC_U32_TYPE_LIST(V)   \
  V(Int8)                   \
  V(Int16)                  \
  V(Int32)

#define ATOMIC_U64_TYPE_LIST(V) \
  ATOMIC_U32_TYPE_LIST(V)       \
  V(Uint64)

#if TAGGED_SIZE_8_BYTES

#define ATOMIC_TAGGED_REPRESENTATION_LIST(V)

#define ATOMIC64_TAGGED_REPRESENTATION_LIST(V) \
  V(kTaggedSigned)                             \
  V(kTaggedPointer)                            \
  V(kTagged)

#else

#define ATOMIC_TAGGED_REPRESENTATION_LIST(V) \
  V(kTaggedSigned)                           \
  V(kTaggedPointer)                          \
  V(kTagged)                                 \
  V(kCompressedPointer)                      \
  V(kCompressed)

#define ATOMIC64_TAGGED_REPRESENTATION_LIST(V)

#endif  // TAGGED_SIZE_8_BYTES

#define ATOMIC_REPRESENTATION_LIST(V) \
  V(kWord8)                           \
  V(kWord16)                          \
  V(kWord32)

#define ATOMIC64_REPRESENTATION_LIST(V) \
  ATOMIC_REPRESENTATION_LIST(V)         \
  V(kWord64)

#define ATOMIC_PAIR_BINOP_LIST(V) \
  V(Add)                          \
  V(Sub)                          \
  V(And)                          \
  V(Or)                           \
  V(Xor)                          \
  V(Exchange)

#define SIMD_LANE_OP_LIST(V) \
  V(F64x2, 2)                \
  V(F32x4, 4)                \
  V(I64x2, 2)                \
  V(I32x4, 4)                \
  V(F16x8, 8)                \
  V(I16x8, 8)                \
  V(I8x16, 16)

#define SIMD_I64x2_LANES(V) V(0) V(1)

#define SIMD_I32x4_LANES(V) SIMD_I64x2_LANES(V) V(2) V(3)

#define SIMD_I16x8_LANES(V) SIMD_I32x4_LANES(V) V(4) V(5) V(6) V(7)

#define SIMD_I8x16_LANES(V) \
  SIMD_I16x8_LANES(V) V(8) V(9) V(10) V(11) V(12) V(13) V(14) V(15)

#define STACK_SLOT_CACHED_SIZES_ALIGNMENTS_LIST(V) \
  V(4, 0, false)                                   \
  V(8, 0, false) V(16, 0, false) V(4, 4, false) V(8, 8, false) V(16, 16, false)

struct StackSlotOperator : public Operator1<StackSlotRepresentation> {
  explicit StackSlotOperator(int size, int alignment, bool is_tagged)
      : Operator1<StackSlotRepresentation>(
            IrOpcode::kStackSlot, Operator::kNoDeopt | Operator::kNoThrow,
            "StackSlot", 0, 0, 0, 1, 0, 0,
            StackSlotRepresentation(size, alignment, is_tagged)) {}
};

struct MachineOperatorGlobalCache {
#define PURE(Name, properties, value_input_count, control_input_count,         \
             output_count)                                                     \
  struct Name##Operator final : public Operator {                              \
    Name##Operator()                                                           \
        : Operator(IrOpcode::k##Name, Operator::kPure | properties, #Name,     \
                   value_input_count, 0, control_input_count, output_count, 0, \
                   0) {}                                                       \
  };                                                                           \
  Name##Operator k##Name;
  MACHINE_PURE_OP_LIST(PURE)
  struct NormalWord32SarOperator final : public Operator1<ShiftKind> {
    NormalWord32SarOperator()
        : Operator1<ShiftKind>(IrOpcode::kWord32Sar, Operator::kPure,
                               "Word32Sar", 2, 0, 0, 1, 0, 0,
                               ShiftKind::kNormal) {}
  };
  NormalWord32SarOperator kNormalWord32Sar;
  struct ShiftOutZerosWord32SarOperator final : public Operator1<ShiftKind> {
    ShiftOutZerosWord32SarOperator()
        : Operator1<ShiftKind>(IrOpcode::kWord32Sar, Operator::kPure,
                               "Word32Sar", 2, 0, 0, 1, 0, 0,
                               ShiftKind::kShiftOutZeros) {}
  };
  ShiftOutZerosWord32SarOperator kShiftOutZerosWord32Sar;
  struct NormalWord64SarOperator final : public Operator1<ShiftKind> {
    NormalWord64SarOperator()
        : Operator1<ShiftKind>(IrOpcode::kWord64Sar, Operator::kPure,
                               "Word64Sar", 2, 0, 0, 1, 0, 0,
                               ShiftKind::kNormal) {}
  };
  NormalWord64SarOperator kNormalWord64Sar;
  struct ShiftOutZerosWord64SarOperator final : public Operator1<ShiftKind> {
    ShiftOutZerosWord64SarOperator()
        : Operator1<ShiftKind>(IrOpcode::kWord64Sar, Operator::kPure,
                               "Word64Sar", 2, 0, 0, 1, 0, 0,
                               ShiftKind::kShiftOutZeros) {}
  };
  ShiftOutZerosWord64SarOperator kShiftOutZerosWord64Sar;

  struct ArchitectureDefaultTruncateFloat32ToUint32Operator final
      : public Operator1<TruncateKind> {
    ArchitectureDefaultTruncateFloat32ToUint32Operator()
        : Operator1<TruncateKind>(IrOpcode::kTruncateFloat32ToUint32,
                                  Operator::kPure, "TruncateFloat32ToUint32", 1,
                                  0, 0, 1, 0, 0,
                                  TruncateKind::kArchitectureDefault) {}
  };
  ArchitectureDefaultTruncateFloat32ToUint32Operator
      kArchitectureDefaultTruncateFloat32ToUint32;
  struct SetOverflowToMinTruncateFloat32ToUint32Operator final
      : public Operator1<TruncateKind> {
    SetOverflowToMinTruncateFloat32ToUint32Operator()
        : Operator1<TruncateKind>(IrOpcode::kTruncateFloat32ToUint32,
                                  Operator::kPure, "TruncateFloat32ToUint32", 1,
                                  0, 0, 1, 0, 0,
                                  TruncateKind::kSetOverflowToMin) {}
  };
  SetOverflowToMinTruncateFloat32ToUint32Operator
      kSetOverflowToMinTruncateFloat32ToUint32;

  struct ArchitectureDefaultTruncateFloat32ToInt32Operator final
      : public Operator1<TruncateKind> {
    ArchitectureDefaultTruncateFloat32ToInt32Operator()
        : Operator1<TruncateKind>(IrOpcode::kTruncateFloat32ToInt32,
                                  Operator::kPure, "TruncateFloat32ToInt32", 1,
                                  0, 0, 1, 0, 0,
                                  TruncateKind::kArchitectureDefault) {}
  };
  ArchitectureDefaultTruncateFloat32ToInt32Operator
      kArchitectureDefaultTruncateFloat32ToInt32;
  struct SetOverflowToMinTruncateFloat32ToInt32Operator final
      : public Operator1<TruncateKind> {
    SetOverflowToMinTruncateFloat32ToInt32Operator()
        : Operator1<TruncateKind>(IrOpcode::kTruncateFloat32ToInt32,
                                  Operator::kPure, "TruncateFloat32ToInt32", 1,
                                  0, 0, 1, 0, 0,
                                  TruncateKind::kSetOverflowToMin) {}
  };
  SetOverflowToMinTruncateFloat32ToInt32Operator
      kSetOverflowToMinTruncateFloat32ToInt32;

  struct ArchitectureDefaultTruncateFloat64ToInt64Operator final
      : public Operator1<TruncateKind> {
    ArchitectureDefaultTruncateFloat64ToInt64Operator()
        : Operator1(IrOpcode::kTruncateFloat64ToInt64, Operator::kPure,
                    "TruncateFloat64ToInt64", 1, 0, 0, 1, 0, 0,
                    TruncateKind::kArchitectureDefault) {}
  };
  ArchitectureDefaultTruncateFloat64ToInt64Operator
      kArchitectureDefaultTruncateFloat64ToInt64;
  struct SetOverflowToMinTruncateFloat64ToInt64Operator final
      : public Operator1<TruncateKind> {
    SetOverflowToMinTruncateFloat64ToInt64Operator()
        : Operator1(IrOpcode::kTruncateFloat64ToInt64, Operator::kPure,
                    "TruncateFloat64ToInt64", 1, 0, 0, 1, 0, 0,
                    TruncateKind::kSetOverflowToMin) {}
  };
  SetOverflowToMinTruncateFloat64ToInt64Operator
      kSetOverflowToMinTruncateFloat64ToInt64;
  PURE_OPTIONAL_OP_LIST(PURE)
#undef PURE

#define OVERFLOW_OP(Name, properties)                                        \
  struct Name##Operator final : public Operator {                            \
    Name##Operator()                                                         \
        : Operator(IrOpcode::k##Name,                                        \
                   Operator::kEliminatable | Operator::kNoRead | properties, \
                   #Name, 2, 0, 1, 2, 0, 0) {}                               \
  };                                                                         \
  Name##Operator k##Name;
  OVERFLOW_OP_LIST(OVERFLOW_OP)
#undef OVERFLOW_OP

// ProtectedLoad and LoadTrapOnNull are not marked kNoWrite, so potentially
// trapping loads are not eliminated if their result is unused.
#define LOAD(Type)                                                             \
  struct Load##Type##Operator final : public Operator1<LoadRepresentation> {   \
    Load##Type##Operator()                                                     \
        : Operator1<LoadRepresentation>(IrOpcode::kLoad,                       \
                                        Operator::kEliminatable, "Load", 2, 1, \
                                        1, 1, 1, 0, MachineType::Type()) {}    \
  };                                                                           \
  struct UnalignedLoad##Type##Operator final                                   \
      : public Operator1<LoadRepresentation> {                                 \
    UnalignedLoad##Type##Operator()                                            \
        : Operator1<LoadRepresentation>(                                       \
              IrOpcode::kUnalignedLoad, Operator::kEliminatable,               \
              "UnalignedLoad", 2, 1, 1, 1, 1, 0, MachineType::Type()) {}       \
  };                                                                           \
  struct ProtectedLoad##Type##Operator final                                   \
      : public Operator1<LoadRepresentation> {                                 \
    ProtectedLoad##Type##Operator()                                            \
        : Operator1<LoadRepresentation>(                                       \
              IrOpcode::kProtectedLoad,                                        \
              Operator::kNoDeopt | Operator::kNoThrow, "ProtectedLoad", 2, 1,  \
              1, 1, 1, 0, MachineType::Type()) {}                              \
  };                                                                           \
  struct LoadTrapOnNull##Type##Operator final                                  \
      : public Operator1<LoadRepresentation> {                                 \
    LoadTrapOnNull##Type##Operator()                                           \
        : Operator1<LoadRepresentation>(                                       \
              IrOpcode::kLoadTrapOnNull,                                       \
              Operator::kNoDeopt | Operator::kNoThrow, "LoadTrapOnNull", 2, 1, \
              1, 1, 1, 0, MachineType::Type()) {}                              \
  };                                                                           \
  struct LoadImmutable##Type##Operator final                                   \
      : public Operator1<LoadRepresentation> {                                 \
    LoadImmutable##Type##Operator()                                            \
        : Operator1<LoadRepresentation>(IrOpcode::kLoadImmutable,              \
                                        Operator::kPure, "LoadImmutable", 2,   \
                                        0, 0, 1, 0, 0, MachineType::Type()) {} \
  };                                                                           \
  Load##Type##Operator kLoad##Type;                                            \
  UnalignedLoad##Type##Operator kUnalignedLoad##Type;                          \
  ProtectedLoad##Type##Operator kProtectedLoad##Type;                          \
  LoadTrapOnNull##Type##Operator kLoadTrapOnNull##Type;                        \
  LoadImmutable##Type##Operator kLoadImmutable##Type;
  MACHINE_TYPE_LIST(LOAD)
#undef LOAD

#if V8_ENABLE_WEBASSEMBLY
#define LOAD_TRANSFORM_KIND(TYPE, KIND)                                 \
  struct KIND##LoadTransform##TYPE##Operator final                      \
      : public Operator1<LoadTransformParameters> {                     \
    KIND##LoadTransform##TYPE##Operator()                               \
        : Operator1<LoadTransformParameters>(                           \
              IrOpcode::kLoadTransform,                                 \
              MemoryAccessKind::k##KIND == MemoryAccessKind::kProtected \
                  ? Operator::kNoDeopt | Operator::kNoThrow             \
                  : Operator::kEliminatable,                            \
              #KIND "LoadTransform", 2, 1, 1, 1, 1, 0,                  \
              LoadTransformParameters{MemoryAccessKind::k##KIND,        \
                                      LoadTransformation::k##TYPE}) {}  \
  };                                                                    \
  KIND##LoadTransform##TYPE##Operator k##KIND##LoadTransform##TYPE;

#define LOAD_TRANSFORM(TYPE)           \
  LOAD_TRANSFORM_KIND(TYPE, Normal)    \
  LOAD_TRANSFORM_KIND(TYPE, Unaligned) \
  LOAD_TRANSFORM_KIND(TYPE, Protected)

  LOAD_TRANSFORM_LIST(LOAD_TRANSFORM)
#undef LOAD_TRANSFORM
#undef LOAD_TRANSFORM_KIND
#endif  // V8_ENABLE_WEBASSEMBLY

#define STACKSLOT(Size, Alignment, IsTagged)                               \
  struct StackSlotOfSize##Size##OfAlignment##Alignment##IsTagged##Operator \
      final : public StackSlotOperator {                                   \
    StackSlotOfSize##Size##OfAlignment##Alignment##IsTagged##Operator()    \
        : StackSlotOperator(Size, Alignment, IsTagged) {}                  \
  };                                                                       \
  StackSlotOfSize##Size##OfAlignment##Alignment##IsTagged##Operator        \
      kStackSlotOfSize##Size##OfAlignment##Alignment##IsTagged;
  STACK_SLOT_CACHED_SIZES_ALIGNMENTS_LIST(STACKSLOT)
#undef STACKSLOT

#define STORE(Type)                                                        \
  struct Store##Type##Operator : public Operator1<StoreRepresentation> {   \
    explicit Store##Type##Operator(WriteBarrierKind write_barrier_kind)    \
        : Operator1<StoreRepresentation>(                                  \
              IrOpcode::kStore,                                            \
              Operator::kNoDeopt | Operator::kNoRead | Operator::kNoThrow, \
              "Store", 3, 1, 1, 0, 1, 0,                                   \
              StoreRepresentation(MachineRepresentation::Type,             \
                                  write_barrier_kind)) {}                  \
  };                                                                       \
  struct Store##Type##NoWriteBarrier##Operator final                       \
      : public Store##Type##Operator {                                     \
    Store##Type##NoWriteBarrier##Operator()                                \
        : Store##Type##Operator(kNoWriteBarrier) {}                        \
  };                                                                       \
  struct Store##Type##AssertNoWriteBarrier##Operator final                 \
      : public Store##Type##Operator {                                     \
    Store##Type##AssertNoWriteBarrier##Operator()                          \
        : Store##Type##Operator(kAssertNoWriteBarrier) {}                  \
  };                                                                       \
  struct Store##Type##MapWriteBarrier##Operator final                      \
      : public Store##Type##Operator {                                     \
    Store##Type##MapWriteBarrier##Operator()                               \
        : Store##Type##Operator(kMapWriteBarrier) {}                       \
  };                                                                       \
  struct Store##Type##PointerWriteBarrier##Operator final                  \
      : public Store##Type##Operator {                                     \
    Store##Type##PointerWriteBarrier##Operator()                           \
        : Store##Type##Operator(kPointerWriteBarrier) {}                   \
  };                                                                       \
  struct Store##Type##EphemeronKeyWriteBarrier##Operator final             \
      : public Store##Type##Operator {                                     \
    Store##Type##EphemeronKeyWriteBarrier##Operator()                      \
        : Store##Type##Operator(kEphemeronKeyWriteBarrier) {}              \
  };                                                                       \
  struct Store##Type##FullWriteBarrier##Operator final                     \
      : public Store##Type##Operator {                                     \
    Store##Type##FullWriteBarrier##Operator()                              \
        : Store##Type##Operator(kFullWriteBarrier) {}                      \
  };                                                                       \
  struct UnalignedStore##Type##Operator final                              \
      : public Operator1<UnalignedStoreRepresentation> {                   \
    UnalignedStore##Type##Operator()                                       \
        : Operator1<UnalignedStoreRepresentation>(                         \
              IrOpcode::kUnalignedStore,                                   \
              Operator::kNoDeopt | Operator::kNoRead | Operator::kNoThrow, \
              "UnalignedStore", 3, 1, 1, 0, 1, 0,                          \
              MachineRepresentation::Type) {}                              \
  };                                                                       \
  struct ProtectedStore##Type##Operator                                    \
      : public Operator1<StoreRepresentation> {                            \
    explicit ProtectedStore##Type##Operator()                              \
        : Operator1<StoreRepresentation>(                                  \
              IrOpcode::kProtectedStore,                                   \
              Operator::kNoDeopt | Operator::kNoRead | Operator::kNoThrow, \
              "ProtectedStore", 3, 1, 1, 0, 1, 0,                          \
              StoreRepresentation(MachineRepresentation::Type,             \
                                  kNoWriteBarrier)) {}                     \
  };                                                                       \
  struct StoreTrapOnNull##Type##FullWriteBarrier##Operator                 \
      : public Operator1<StoreRepresentation> {                            \
    explicit StoreTrapOnNull##Type##FullWriteBarrier##Operator()           \
        : Operator1<StoreRepresentation>(                                  \
              IrOpcode::kStoreTrapOnNull,                                  \
              Operator::kNoDeopt | Operator::kNoRead | Operator::kNoThrow, \
              "StoreTrapOnNull", 3, 1, 1, 0, 1, 0,                         \
              StoreRepresentation(MachineRepresentation::Type,             \
                                  kFullWriteBarrier)) {}                   \
  };                                                                       \
  struct StoreTrapOnNull##Type##NoWriteBarrier##Operator                   \
      : public Operator1<StoreRepresentation> {                            \
    explicit StoreTrapOnNull##Type##NoWriteBarrier##Operator()             \
        : Operator1<StoreRepresentation>(                                  \
              IrOpcode::kStoreTrapOnNull,                                  \
              Operator::kNoDeopt | Operator::kNoRead | Operator::kNoThrow, \
              "StoreTrapOnNull", 3, 1, 1, 0, 1, 0,                         \
              StoreRepresentation(MachineRepresentation::Type,             \
                                  kNoWriteBarrier)) {}                     \
  };                                                                       \
  Store##Type##NoWriteBarrier##Operator kStore##Type##NoWriteBarrier;      \
  Store##Type##AssertNoWriteBarrier##Operator                              \
      kStore##Type##AssertNoWriteBarrier;                                  \
  Store##Type##MapWriteBarrier##Operator kStore##Type##MapWriteBarrier;    \
  Store##Type##PointerWriteBarrier##Operator                               \
      kStore##Type##PointerWriteBarrier;                                   \
  Store##Type##EphemeronKeyWriteBarrier##Operator                          \
      kStore##Type##EphemeronKeyWriteBarrier;                              \
  Store##Type##FullWriteBarrier##Operator kStore##Type##FullWriteBarrier;  \
  UnalignedStore##Type##Operator kUnalignedStore##Type;                    \
  ProtectedStore##Type##Operator kProtectedStore##Type;                    \
  StoreTrapOnNull##Type##FullWriteBarrier##Operator                        \
      kStoreTrapOnNull##Type##FullWriteBarrier;                            \
  StoreTrapOnNull##Type##NoWriteBarrier##Operator                          \
      kStoreTrapOnNull##Type##NoWriteBarrier;
  MACHINE_REPRESENTATION_LIST(STORE)
#undef STORE

  friend std::ostream& operator<<(std::ostream& out,
                                  const StorePairRepresentation rep) {
    out << rep.first << "," << rep.second;
    return out;
  }

#define STORE_PAIR(Type1, Type2)                                           \
  struct StorePair##Type1##Type2##Operator                                 \
      : public Operator1<StorePairRepresentation> {                        \
    explicit StorePair##Type1##Type2##Operator(                            \
        WriteBarrierKind write_barrier_kind1,                              \
        WriteBarrierKind write_barrier_kind2)                              \
        : Operator1<StorePairRepresentation>(                              \
              IrOpcode::kStorePair,                                        \
              Operator::kNoDeopt | Operator::kNoRead | Operator::kNoThrow, \
              "StorePair", 4, 1, 1, 0, 1, 0,                               \
              {                                                            \
                  StoreRepresentation(MachineRepresentation::Type1,        \
                                      write_barrier_kind1),                \
                  StoreRepresentation(MachineRepresentation::Type2,        \
                                      write_barrier_kind2),                \
              }) {}                                                        \
  };                                                                       \
  struct StorePair##Type1##Type2##NoWriteBarrier##Operator final           \
      : public StorePair##Type1##Type2##Operator {                         \
    StorePair##Type1##Type2##NoWriteBarrier##Operator()                    \
        : StorePair##Type1##Type2                                          \
          ##Operator(kNoWriteBarrier, kNoWriteBarrier) {}                  \
  };                                                                       \
  StorePair##Type1##Type2##NoWriteBarrier##Operator                        \
      kStorePair##Type1##Type2##NoWriteBarrier;

  STORE_PAIR_MACHINE_REPRESENTATION_LIST(STORE_PAIR)
#undef STORE_PAIR

  // Indirect pointer stores have an additional value input (the
  // IndirectPointerTag associated with the field being stored to), but
  // otherwise are identical to regular stores.
  struct StoreIndirectPointerOperator : public Operator1<StoreRepresentation> {
    explicit StoreIndirectPointerOperator(WriteBarrierKind write_barrier_kind)
        : Operator1<StoreRepresentation>(
              IrOpcode::kStoreIndirectPointer,
              Operator::kNoDeopt | Operator::kNoRead | Operator::kNoThrow,
              "StoreIndirectPointer", 4, 1, 1, 0, 1, 0,
              StoreRepresentation(MachineRepresentation::kIndirectPointer,
                                  write_barrier_kind)) {}
  };
  struct StoreIndirectPointerNoWriteBarrierOperator final
      : public StoreIndirectPointerOperator {
    StoreIndirectPointerNoWriteBarrierOperator()
        : StoreIndirectPointerOperator(kNoWriteBarrier) {}
  };
  struct StoreIndirectPointerWithIndirectPointerWriteBarrierOperator final
      : public StoreIndirectPointerOperator {
    StoreIndirectPointerWithIndirectPointerWriteBarrierOperator()
        : StoreIndirectPointerOperator(kIndirectPointerWriteBarrier) {}
  };

  StoreIndirectPointerNoWriteBarrierOperator
      kStoreIndirectPointerNoWriteBarrier;
  StoreIndirectPointerWithIndirectPointerWriteBarrierOperator
      kStoreIndirectPointerIndirectPointerWriteBarrier;

#define ATOMIC_LOAD_WITH_KIND(Type, Kind)                           \
  struct Word32SeqCstLoad##Type##Kind##Operator                     \
      : public Operator1<AtomicLoadParameters> {                    \
    Word32SeqCstLoad##Type##Kind##Operator()                        \
        : Operator1<AtomicLoadParameters>(                          \
              IrOpcode::kWord32AtomicLoad, Operator::kNoProperties, \
              "Word32AtomicLoad", 2, 1, 1, 1, 1, 0,                 \
              AtomicLoadParameters(MachineType::Type(),             \
                                   AtomicMemoryOrder::kSeqCst,      \
                                   MemoryAccessKind::k##Kind)) {}   \
  };                                                                \
  Word32SeqCstLoad##Type##Kind##Operator kWord32SeqCstLoad##Type##Kind;
#define ATOMIC_LOAD(Type)             \
  ATOMIC_LOAD_WITH_KIND(Type, Normal) \
  ATOMIC_LOAD_WITH_KIND(Type, Protected)
  ATOMIC_TYPE_LIST(ATOMIC_LOAD)
#undef ATOMIC_LOAD_WITH_KIND
#undef ATOMIC_LOAD

#define ATOMIC_LOAD_WITH_KIND(Type, Kind)                           \
  struct Word64SeqCstLoad##Type##Kind##Operator                     \
      : public Operator1<AtomicLoadParameters> {                    \
    Word64SeqCstLoad##Type##Kind##Operator()                        \
        : Operator1<AtomicLoadParameters>(                          \
              IrOpcode::kWord64AtomicLoad, Operator::kNoProperties, \
              "Word64AtomicLoad", 2, 1, 1, 1, 1, 0,                 \
              AtomicLoadParameters(MachineType::Type(),             \
                                   AtomicMemoryOrder::kSeqCst,      \
                                   MemoryAccessKind::k##Kind)) {}   \
  };                                                                \
  Word64SeqCstLoad##Type##Kind##Operator kWord64SeqCstLoad##Type##Kind;
#define ATOMIC_LOAD(Type)             \
  ATOMIC_LOAD_WITH_KIND(Type, Normal) \
  ATOMIC_LOAD_WITH_KIND(Type, Protected)
  ATOMIC_U64_TYPE_LIST(ATOMIC_LOAD)
#undef ATOMIC_LOAD_WITH_KIND
#undef ATOMIC_LOAD

#define ATOMIC_STORE_WITH_KIND(Type, Kind)                                 \
  struct Word32SeqCstStore##Type##Kind##Operator                           \
      : public Operator1<AtomicStoreParameters> {                          \
    Word32SeqCstStore##Type##Kind##Operator()                              \
        : Operator1<AtomicStoreParameters>(                                \
              IrOpcode::kWord32AtomicStore,                                \
              Operator::kNoDeopt | Operator::kNoRead | Operator::kNoThrow, \
              "Word32AtomicStore", 3, 1, 1, 0, 1, 0,                       \
              AtomicStoreParameters(MachineRepresentation::Type,           \
                                    kNoWriteBarrier,                       \
                                    AtomicMemoryOrder::kSeqCst,            \
                                    MemoryAccessKind::k##Kind)) {}         \
  };                                                                       \
  Word32SeqCstStore##Type##Kind##Operator kWord32SeqCstStore##Type##Kind;
#define ATOMIC_STORE(Type)             \
  ATOMIC_STORE_WITH_KIND(Type, Normal) \
  ATOMIC_STORE_WITH_KIND(Type, Protected)
  ATOMIC_REPRESENTATION_LIST(ATOMIC_STORE)
#undef ATOMIC_STORE_WITH_KIND
#undef ATOMIC_STORE

#define ATOMIC_STORE_WITH_KIND(Type, Kind)                                 \
  struct Word64SeqCstStore##Type##Kind##Operator                           \
      : public Operator1<AtomicStoreParameters> {                          \
    Word64SeqCstStore##Type##Kind##Operator()                              \
        : Operator1<AtomicStoreParameters>(                                \
              IrOpcode::kWord64AtomicStore,                                \
              Operator::kNoDeopt | Operator::kNoRead | Operator::kNoThrow, \
              "Word64AtomicStore", 3, 1, 1, 0, 1, 0,                       \
              AtomicStoreParameters(MachineRepresentation::Type,           \
                                    kNoWriteBarrier,                       \
                                    AtomicMemoryOrder::kSeqCst,            \
                                    MemoryAccessKind::k##Kind)) {}         \
  };                                                                       \
  Word64SeqCstStore##Type##Kind##Operator kWord64SeqCstStore##Type##Kind;
#define ATOMIC_STORE(Type)             \
  ATOMIC_STORE_WITH_KIND(Type, Normal) \
  ATOMIC_STORE_WITH_KIND(Type, Protected)
  ATOMIC64_REPRESENTATION_LIST(ATOMIC_STORE)
#undef ATOMIC_STORE_WITH_KIND
#undef ATOMIC_STORE

#define ATOMIC_OP(op, type, kind)                                              \
  struct op##type##kind##Operator : public Operator1<AtomicOpParameters> {     \
    op##type##kind##Operator()                                                 \
        : Operator1<AtomicOpParameters>(IrOpcode::k##op,                       \
                                 Operator::kNoDeopt | Operator::kNoThrow, #op, \
                                 3, 1, 1, 1, 1, 0,                             \
                                 AtomicOpParameters(MachineType::type(),       \
                                                    MemoryAccessKind::k##kind) \
                                 ){}                                           \
  };                                                                           \
  op##type##kind##Operator k##op##type##kind;
#define ATOMIC_OP_LIST_WITH_KIND(type, kind) \
  ATOMIC_OP(Word32AtomicAdd, type, kind)     \
  ATOMIC_OP(Word32AtomicSub, type, kind)     \
  ATOMIC_OP(Word32AtomicAnd, type, kind)     \
  ATOMIC_OP(Word32AtomicOr, type, kind)      \
  ATOMIC_OP(Word32AtomicXor, type, kind)     \
  ATOMIC_OP(Word32AtomicExchange, type, kind)
#define ATOMIC_OP_LIST(type)             \
  ATOMIC_OP_LIST_WITH_KIND(type, Normal) \
  ATOMIC_OP_LIST_WITH_KIND(type, Protected)
  ATOMIC_TYPE_LIST(ATOMIC_OP_LIST)
#undef ATOMIC_OP_LIST_WITH_KIND
#undef ATOMIC_OP_LIST
#define ATOMIC64_OP_LIST_WITH_KIND(type, kind) \
  ATOMIC_OP(Word64AtomicAdd, type, kind)       \
  ATOMIC_OP(Word64AtomicSub, type, kind)       \
  ATOMIC_OP(Word64AtomicAnd, type, kind)       \
  ATOMIC_OP(Word64AtomicOr, type, kind)        \
  ATOMIC_OP(Word64AtomicXor, type, kind)       \
  ATOMIC_OP(Word64AtomicExchange, type, kind)
#define ATOMIC64_OP_LIST(type)             \
  ATOMIC64_OP_LIST_WITH_KIND(type, Normal) \
  ATOMIC64_OP_LIST_WITH_KIND(type, Protected)
  ATOMIC_U64_TYPE_LIST(ATOMIC64_OP_LIST)
#undef ATOMIC64_OP_LIST_WITH_KIND
#undef ATOMIC64_OP_LIST
#undef ATOMIC_OP

#define ATOMIC_COMPARE_EXCHANGE_WITH_KIND(Type, Kind)                          \
  struct Word32AtomicCompareExchange##Type##Kind##Operator                     \
      : public Operator1<AtomicOpParameters> {                                 \
    Word32AtomicCompareExchange##Type##Kind##Operator()                        \
        : Operator1<AtomicOpParameters>(                                       \
                                 IrOpcode::kWord32AtomicCompareExchange,       \
                                 Operator::kNoDeopt | Operator::kNoThrow,      \
                                 "Word32AtomicCompareExchange", 4, 1, 1, 1, 1, \
                                 0,                                            \
                                 AtomicOpParameters(MachineType::Type(),       \
                                                    MemoryAccessKind::k##Kind) \
          ) {}                                                                 \
  };                                                                           \
  Word32AtomicCompareExchange##Type##Kind##Operator                            \
      kWord32AtomicCompareExchange##Type##Kind;
#define ATOMIC_COMPARE_EXCHANGE(Type)             \
  ATOMIC_COMPARE_EXCHANGE_WITH_KIND(Type, Normal) \
  ATOMIC_COMPARE_EXCHANGE_WITH_KIND(Type, Protected)
  ATOMIC_TYPE_LIST(ATOMIC_COMPARE_EXCHANGE)
#undef ATOMIC_COMPARE_EXCHANGE_WITH_KIND
#undef ATOMIC_COMPARE_EXCHANGE

#define ATOMIC_COMPARE_EXCHANGE_WITH_KIND(Type, Kind)                          \
  struct Word64AtomicCompareExchange##Type##Kind##Operator                     \
      : public Operator1<AtomicOpParameters> {                                 \
    Word64AtomicCompareExchange##Type##Kind##Operator()                        \
        : Operator1<AtomicOpParameters>(                                       \
                                 IrOpcode::kWord64AtomicCompareExchange,       \
                                 Operator::kNoDeopt | Operator::kNoThrow,      \
                                 "Word64AtomicCompareExchange", 4, 1, 1, 1, 1, \
                                 0,                                            \
                                 AtomicOpParameters(MachineType::Type(),       \
                                                    MemoryAccessKind::k##Kind) \
          ) {}                                                                 \
  };                                                                           \
  Word64AtomicCompareExchange##Type##Kind##Operator                            \
      kWord64AtomicCompareExchange##Type##Kind;
#define ATOMIC_COMPARE_EXCHANGE(Type)             \
  ATOMIC_COMPARE_EXCHANGE_WITH_KIND(Type, Normal) \
  ATOMIC_COMPARE_EXCHANGE_WITH_KIND(Type, Protected)
  ATOMIC_U64_TYPE_LIST(ATOMIC_COMPARE_EXCHANGE)
#undef ATOMIC_COMPARE_EXCHANGE_WITH_KIND
#undef ATOMIC_COMPARE_EXCHANGE

  struct Word32SeqCstPairLoadOperator : public Operator1<AtomicMemoryOrder> {
    Word32SeqCstPairLoadOperator()
        : Operator1<AtomicMemoryOrder>(IrOpcode::kWord32AtomicPairLoad,
                                       Operator::kNoDeopt | Operator::kNoThrow,
                                       "Word32AtomicPairLoad", 2, 1, 1, 2, 1, 0,
                                       AtomicMemoryOrder::kSeqCst) {}
  };
  Word32SeqCstPairLoadOperator kWord32SeqCstPairLoad;

  struct Word32SeqCstPairStoreOperator : public Operator1<AtomicMemoryOrder> {
    Word32SeqCstPairStoreOperator()
        : Operator1<AtomicMemoryOrder>(IrOpcode::kWord32AtomicPairStore,
                                       Operator::kNoDeopt | Operator::kNoThrow,
                                       "Word32AtomicPairStore", 4, 1, 1, 0, 1,
                                       0, AtomicMemoryOrder::kSeqCst) {}
  };
  Word32SeqCstPairStoreOperator kWord32SeqCstPairStore;

#define ATOMIC_PAIR_OP(op)                                      \
  struct Word32AtomicPair##op##Operator : public Operator {     \
    Word32AtomicPair##op##Operator()                            \
        : Operator(IrOpcode::kWord32AtomicPair##op,             \
                   Operator::kNoDeopt | Operator::kNoThrow,     \
                   "Word32AtomicPair" #op, 4, 1, 1, 2, 1, 0) {} \
  };                                                            \
  Word32AtomicPair##op##Operator kWord32AtomicPair##op;
  ATOMIC_PAIR_BINOP_LIST(ATOMIC_PAIR_OP)
#undef ATOMIC_PAIR_OP
#undef ATOMIC_PAIR_BINOP_LIST

  struct Word32AtomicPairCompareExchangeOperator : public Operator {
    Word32AtomicPairCompareExchangeOperator()
        : Operator(IrOpcode::kWord32AtomicPairCompareExchange,
                   Operator::kNoDeopt | Operator::kNoThrow,
                   "Word32AtomicPairCompareExchange", 6, 1, 1, 2, 1, 0) {}
  };
  Word32AtomicPairCompareExchangeOperator kWord32AtomicPairCompareExchange;

  template <AtomicMemoryOrder order>
  struct MemoryBarrierOperator : public Operator1<AtomicMemoryOrder> {
    MemoryBarrierOperator()
        : Operator1<AtomicMemoryOrder>(
              IrOpcode::kMemoryBarrier, Operator::kNoDeopt | Operator::kNoThrow,
              "SeqCstMemoryBarrier", 0, 1, 1, 0, 1, 0, order) {}
  };
  MemoryBarrierOperator<AtomicMemoryOrder::kSeqCst> kSeqCstMemoryBarrier;
  MemoryBarrierOperator<AtomicMemoryOrder::kAcqRel> kAcqRelMemoryBarrier;

  // The {BitcastWordToTagged} operator must not be marked as pure (especially
  // not idempotent), because otherwise the splitting logic in the Scheduler
  // might decide to split these operators, thus potentially creating live
  // ranges of allocation top across calls or other things that might allocate.
  // See https://bugs.chromium.org/p/v8/issues/detail?id=6059 for more details.
  struct BitcastWordToTaggedOperator : public Operator {
    BitcastWordToTaggedOperator()
        : Operator(IrOpcode::kBitcastWordToTagged,
                   Operator::kEliminatable | Operator::kNoWrite,
                   "BitcastWordToTagged", 1, 1, 1, 1, 1, 0) {}
  };
  BitcastWordToTaggedOperator kBitcastWordToTagged;

  struct BitcastTaggedToWordOperator : public Operator {
    BitcastTaggedToWordOperator()
        : Operator(IrOpcode::kBitcastTaggedToWord,
                   Operator::kEliminatable | Operator::kNoWrite,
                   "BitcastTaggedToWord", 1, 1, 1, 1, 1, 0) {}
  };
  BitcastTaggedToWordOperator kBitcastTaggedToWord;

  struct BitcastMaybeObjectToWordOperator : public Operator {
    BitcastMaybeObjectToWordOperator()
        : Operator(IrOpcode::kBitcastTaggedToWord,
                   Operator::kEliminatable | Operator::kNoWrite,
                   "BitcastMaybeObjectToWord", 1, 1, 1, 1, 1, 0) {}
  };
  BitcastMaybeObjectToWordOperator kBitcastMaybeObjectToWord;

  struct AbortCSADcheckOperator : public Operator {
    AbortCSADcheckOperator()
        : Operator(IrOpcode::kAbortCSADcheck, Operator::kNoThrow,
                   "AbortCSADcheck", 1, 1, 1, 0, 1, 0) {}
  };
  AbortCSADcheckOperator kAbortCSADcheck;

  struct DebugBreakOperator : public Operator {
    DebugBreakOperator()
        : Operator(IrOpcode::kDebugBreak, Operator::kNoThrow, "DebugBreak", 0,
                   1, 1, 0, 1, 0) {}
  };
  DebugBreakOperator kDebugBreak;

  struct StackPointerGreaterThanOperator : public Operator1<StackCheckKind> {
    explicit StackPointerGreaterThanOperator(StackCheckKind kind)
        : Operator1<StackCheckKind>(
              IrOpcode::kStackPointerGreaterThan, Operator::kEliminatable,
              "StackPointerGreaterThan", 1, 1, 0, 1, 1, 0, kind) {}
  };
#define STACK_POINTER_GREATER_THAN(Kind)                              \
  struct StackPointerGreaterThan##Kind##Operator final                \
      : public StackPointerGreaterThanOperator {                      \
    StackPointerGreaterThan##Kind##Operator()                         \
        : StackPointerGreaterThanOperator(StackCheckKind::k##Kind) {} \
  };                                                                  \
  StackPointerGreaterThan##Kind##Operator kStackPointerGreaterThan##Kind;

  STACK_POINTER_GREATER_THAN(JSFunctionEntry)
  STACK_POINTER_GREATER_THAN(CodeStubAssembler)
  STACK_POINTER_GREATER_THAN(Wasm)
#undef STACK_POINTER_GREATER_THAN

#if V8_ENABLE_WEBASSEMBLY
  struct I8x16SwizzleOperator final : public Operator1<bool> {
    I8x16SwizzleOperator()
        : Operator1<bool>(IrOpcode::kI8x16Swizzle, Operator::kPure,
                          "I8x16Swizzle", 2, 0, 0, 1, 0, 0, false) {}
  };
  I8x16SwizzleOperator kI8x16Swizzle;
  struct I8x16RelaxedSwizzleOperator final : public Operator1<bool> {
    I8x16RelaxedSwizzleOperator()
        : Operator1<bool>(IrOpcode::kI8x16Swizzle, Operator::kPure,
                          "I8x16RelaxedSwizzle", 2, 0, 0, 1, 0, 0, true) {}
  };
  I8x16RelaxedSwizzleOperator kI8x16RelaxedSwizzle;
#endif  // V8_ENABLE_WEBASSEMBLY
};

struct CommentOperator : public Operator1<const char*> {
  explicit CommentOperator(const char* msg)
      : Operator1<const char*>(IrOpcode::kComment,
                               Operator::kNoThrow | Operator::kNoWrite,
                               "Comment", 0, 1, 1, 0, 1, 0, msg) {}
};

namespace {
DEFINE_LAZY_LEAKY_OBJECT_GETTER(MachineOperatorGlobalCache,
                                GetMachineOperatorGlobalCache)
}

MachineOperatorBuilder::MachineOperatorBuilder(
    Zone* zone, MachineRepresentation word, Flags flags,
    AlignmentRequirements alignmentRequirements)
    : zone_(zone),
      cache_(*GetMachineOperatorGlobalCache()),
      word_(word),
      flags_(flags),
      alignment_requirements_(alignmentRequirements) {
  DCHECK(word == MachineRepresentation::kWord32 ||
         word == MachineRepresentation::kWord64);
}

const Operator* MachineOperatorBuilder::UnalignedLoad(LoadRepresentation rep) {
#define LOAD(Type)                       \
  if (rep == MachineType::Type()) {      \
    return &cache_.kUnalignedLoad##Type; \
  }
  MACHINE_TYPE_LIST(LOAD)
#undef LOAD
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::UnalignedStore(
    UnalignedStoreRepresentation rep) {
  switch (rep) {
#define STORE(kRep)                 \
  case MachineRepresentation::kRep: \
    return &cache_.kUnalignedStore##kRep;
    MACHINE_REPRESENTATION_LIST(STORE)
#undef STORE
    case MachineRepresentation::kBit:
    case MachineRepresentation::kProtectedPointer:
    case MachineRepresentation::kIndirectPointer:
    case MachineRepresentation::kNone:
      UNREACHABLE();
  }
}

#define PURE(Name, properties, value_input_count, control_input_count, \
             output_count)                                             \
  const Operator* MachineOperatorBuilder::Name() { return &cache_.k##Name; }
MACHINE_PURE_OP_LIST(PURE)
#undef PURE

const Operator* MachineOperatorBuilder::Word32Sar(ShiftKind kind) {
  switch (kind) {
    case ShiftKind::kNormal:
      return &cache_.kNormalWord32Sar;
    case ShiftKind::kShiftOutZeros:
      return &cache_.kShiftOutZerosWord32Sar;
  }
}

const Operator* MachineOperatorBuilder::Word64Sar(ShiftKind kind) {
  switch (kind) {
    case ShiftKind::kNormal:
      return &cache_.kNormalWord64Sar;
    case ShiftKind::kShiftOutZeros:
      return &cache_.kShiftOutZerosWord64Sar;
  }
}

const Operator* MachineOperatorBuilder::TruncateFloat32ToUint32(
    TruncateKind kind) {
  switch (kind) {
    case TruncateKind::kArchitectureDefault:
      return &cache_.kArchitectureDefaultTruncateFloat32ToUint32;
    case TruncateKind::kSetOverflowToMin:
      return &cache_.kSetOverflowToMinTruncateFloat32ToUint32;
  }
}

const Operator* MachineOperatorBuilder::TruncateFloat64ToInt64(
    TruncateKind kind) {
  switch (kind) {
    case TruncateKind::kArchitectureDefault:
      return &cache_.kArchitectureDefaultTruncateFloat64ToInt64;
    case TruncateKind::kSetOverflowToMin:
      return &cache_.kSetOverflowToMinTruncateFloat64ToInt64;
  }
}

const Operator* MachineOperatorBuilder::TruncateFloat32ToInt32(
    TruncateKind kind) {
  switch (kind) {
    case TruncateKind::kArchitectureDefault:
      return &cache_.kArchitectureDefaultTruncateFloat32ToInt32;
    case TruncateKind::kSetOverflowToMin:
      return &cache_.kSetOverflowToMinTruncateFloat32ToInt32;
  }
}

#define PURE(Name, properties, value_input_count, control_input_count, \
             output_count)                                             \
  const OptionalOperator MachineOperatorBuilder::Name() {              \
    return OptionalOperator(flags_ & k##Name, &cache_.k##Name);        \
  }
PURE_OPTIONAL_OP_LIST(PURE)
#undef PURE

#define OVERFLOW_OP(Name, properties) \
  const Operator* MachineOperatorBuilder::Name() { return &cache_.k##Name; }
OVERFLOW_OP_LIST(OVERFLOW_OP)
#undef OVERFLOW_OP

const Operator* MachineOperatorBuilder::TraceInstruction(uint32_t markid) {
  return zone_->New<Operator1<uint32_t>>(
      IrOpcode::kTraceInstruction, Operator::kNoDeopt | Operator::kNoThrow,
      "TraceInstruction", 0, 1, 1, 0, 1, 0, markid);
}

const Operator* MachineOperatorBuilder::Load(LoadRepresentation rep) {
  DCHECK(!rep.IsMapWord());
#define LOAD(Type)                  \
  if (rep == MachineType::Type()) { \
    return &cache_.kLoad##Type;     \
  }
  MACHINE_TYPE_LIST(LOAD)
#undef LOAD
  UNREACHABLE();
}

// Represents a load from a position in memory that is known to be immutable,
// e.g. an immutable IsolateRoot or an immutable field of a WasmInstanceObject.
// Because the returned value cannot change through the execution of a function,
// LoadImmutable is a pure operator and does not have effect or control edges.
// Requires that the memory in question has been initialized at function start
// even through inlining.
const Operator* MachineOperatorBuilder::LoadImmutable(LoadRepresentation rep) {
#define LOAD(Type)                       \
  if (rep == MachineType::Type()) {      \
    return &cache_.kLoadImmutable##Type; \
  }
  MACHINE_TYPE_LIST(LOAD)
#undef LOAD
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::ProtectedLoad(LoadRepresentation rep) {
#define LOAD(Type)                       \
  if (rep == MachineType::Type()) {      \
    return &cache_.kProtectedLoad##Type; \
  }
  MACHINE_TYPE_LIST(LOAD)
#undef LOAD
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::LoadTrapOnNull(LoadRepresentation rep) {
#define LOAD(Type)                        \
  if (rep == MachineType::Type()) {       \
    return &cache_.kLoadTrapOnNull##Type; \
  }
  MACHINE_TYPE_LIST(LOAD)
#undef LOAD
  UNREACHABLE();
}

#if V8_ENABLE_WEBASSEMBLY
const Operator* MachineOperatorBuilder::LoadTransform(
    MemoryAccessKind kind, LoadTransformation transform) {
#define LOAD_TRANSFORM_KIND(TYPE, KIND)           \
  if (kind == MemoryAccessKind::k##KIND &&        \
      transform == LoadTransformation::k##TYPE) { \
    return &cache_.k##KIND##LoadTransform##TYPE;  \
  }
#define LOAD_TRANSFORM(TYPE)           \
  LOAD_TRANSFORM_KIND(TYPE, Normal)    \
  LOAD_TRANSFORM_KIND(TYPE, Unaligned) \
  LOAD_TRANSFORM_KIND(TYPE, Protected)

  LOAD_TRANSFORM_LIST(LOAD_TRANSFORM)
#undef LOAD_TRANSFORM
#undef LOAD_TRANSFORM_KIND
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::LoadLane(MemoryAccessKind kind,
                                                 LoadRepresentation rep,
                                                 uint8_t laneidx) {
#define LOAD_LANE_KIND(TYPE, KIND, LANEIDX)                              \
  if (kind == MemoryAccessKind::k##KIND && rep == MachineType::TYPE() && \
      laneidx == LANEIDX) {                                              \
    return zone_->New<Operator1<LoadLaneParameters>>(                    \
        IrOpcode::kLoadLane,                                             \
        MemoryAccessKind::k##KIND == MemoryAccessKind::kProtected        \
            ? Operator::kNoDeopt | Operator::kNoThrow                    \
            : Operator::kEliminatable,                                   \
        "LoadLane", 3, 1, 1, 1, 1, 0,                                    \
        LoadLaneParameters{MemoryAccessKind::k##KIND,                    \
                           LoadRepresentation::TYPE(), LANEIDX});        \
  }

#define LOAD_LANE_T(T, LANE)         \
  LOAD_LANE_KIND(T, Normal, LANE)    \
  LOAD_LANE_KIND(T, Unaligned, LANE) \
  LOAD_LANE_KIND(T, Protected, LANE)

#define LOAD_LANE_INT8(LANE) LOAD_LANE_T(Int8, LANE)
#define LOAD_LANE_INT16(LANE) LOAD_LANE_T(Int16, LANE)
#define LOAD_LANE_INT32(LANE) LOAD_LANE_T(Int32, LANE)
#define LOAD_LANE_INT64(LANE) LOAD_LANE_T(Int64, LANE)

  // Semicolons unnecessary, but helps formatting.
  SIMD_I8x16_LANES(LOAD_LANE_INT8);
  SIMD_I16x8_LANES(LOAD_LANE_INT16);
  SIMD_I32x4_LANES(LOAD_LANE_INT32);
  SIMD_I64x2_LANES(LOAD_LANE_INT64);
#undef LOAD_LANE_INT8
#undef LOAD_LANE_INT16
#undef LOAD_LANE_INT32
#undef LOAD_LANE_INT64
#undef LOAD_LANE_KIND
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::StoreLane(MemoryAccessKind kind,
                                                  MachineRepresentation rep,
                                                  uint8_t laneidx) {
#define STORE_LANE_KIND(REP, KIND, LANEIDX)                          \
  if (kind == MemoryAccessKind::k##KIND &&                           \
      rep == MachineRepresentation::REP && laneidx == LANEIDX) {     \
    return zone_->New<Operator1<StoreLaneParameters>>(               \
        IrOpcode::kStoreLane,                                        \
        Operator::kNoDeopt | Operator::kNoRead | Operator::kNoThrow, \
        "StoreLane", 3, 1, 1, 0, 1, 0,                               \
        StoreLaneParameters{MemoryAccessKind::k##KIND,               \
                            MachineRepresentation::REP, LANEIDX});   \
  }

#define STORE_LANE_T(T, LANE)         \
  STORE_LANE_KIND(T, Normal, LANE)    \
  STORE_LANE_KIND(T, Unaligned, LANE) \
  STORE_LANE_KIND(T, Protected, LANE)

#define STORE_LANE_WORD8(LANE) STORE_LANE_T(kWord8, LANE)
#define STORE_LANE_WORD16(LANE) STORE_LANE_T(kWord16, LANE)
#define STORE_LANE_WORD32(LANE) STORE_LANE_T(kWord32, LANE)
#define STORE_LANE_WORD64(LANE) STORE_LANE_T(kWord64, LANE)

  // Semicolons unnecessary, but helps formatting.
  SIMD_I8x16_LANES(STORE_LANE_WORD8);
  SIMD_I16x8_LANES(STORE_LANE_WORD16);
  SIMD_I32x4_LANES(STORE_LANE_WORD32);
  SIMD_I64x2_LANES(STORE_LANE_WORD64);
#undef STORE_LANE_WORD8
#undef STORE_LANE_WORD16
#undef STORE_LANE_WORD32
#undef STORE_LANE_WORD64
#undef STORE_LANE_KIND
  UNREACHABLE();
}
#endif  // V8_ENABLE_WEBASSEMBLY

const Operator* MachineOperatorBuilder::StackSlot(int size, int alignment,
                                                  bool is_tagged) {
  DCHECK_LE(0, size);
  DCHECK(alignment == 0 || alignment == 4 || alignment == 8 || alignment == 16);
#define CASE_CACHED_SIZE(Size, Alignment, IsTagged)                          \
  if (size == Size && alignment == Alignment && is_tagged == IsTagged) {     \
    return &cache_.kStackSlotOfSize##Size##OfAlignment##Alignment##IsTagged; \
  }

  STACK_SLOT_CACHED_SIZES_ALIGNMENTS_LIST(CASE_CACHED_SIZE)

#undef CASE_CACHED_SIZE
  return zone_->New<StackSlotOperator>(size, alignment, is_tagged);
}

const Operator* MachineOperatorBuilder::StackSlot(MachineRepresentation rep,
                                                  int alignment) {
  return StackSlot(1 << ElementSizeLog2Of(rep), alignment);
}

const Operator* MachineOperatorBuilder::Store(StoreRepresentation store_rep) {
  DCHECK_NE(store_rep.representation(), MachineRepresentation::kMapWord);
  DCHECK_NE(store_rep.representation(),
            MachineRepresentation::kIndirectPointer);
  switch (store_rep.representation()) {
#define STORE(kRep)                                              \
  case MachineRepresentation::kRep:                              \
    switch (store_rep.write_barrier_kind()) {                    \
      case kNoWriteBarrier:                                      \
        return &cache_.k##Store##kRep##NoWriteBarrier;           \
      case kAssertNoWriteBarrier:                                \
        return &cache_.k##Store##kRep##AssertNoWriteBarrier;     \
      case kMapWriteBarrier:                                     \
        return &cache_.k##Store##kRep##MapWriteBarrier;          \
      case kPointerWriteBarrier:                                 \
        return &cache_.k##Store##kRep##PointerWriteBarrier;      \
      case kIndirectPointerWriteBarrier:                         \
        UNREACHABLE();                                           \
      case kEphemeronKeyWriteBarrier:                            \
        return &cache_.k##Store##kRep##EphemeronKeyWriteBarrier; \
      case kFullWriteBarrier:                                    \
        return &cache_.k##Store##kRep##FullWriteBarrier;         \
    }                                                            \
    break;
    MACHINE_REPRESENTATION_LIST(STORE)
#undef STORE
    case MachineRepresentation::kBit:
    case MachineRepresentation::kProtectedPointer:
    case MachineRepresentation::kIndirectPointer:
    case MachineRepresentation::kNone:
      UNREACHABLE();
  }
}

const Operator* MachineOperatorBuilder::StoreIndirectPointer(
    WriteBarrierKind write_barrier_kind) {
  switch (write_barrier_kind) {
    case kNoWriteBarrier:
      return &cache_.kStoreIndirectPointerNoWriteBarrier;
    case kIndirectPointerWriteBarrier:
      return &cache_.kStoreIndirectPointerIndirectPointerWriteBarrier;
    default:
      UNREACHABLE();
  }
}

std::optional<const Operator*> MachineOperatorBuilder::TryStorePair(
    StoreRepresentation store_rep1, StoreRepresentation store_rep2) {
  DCHECK_NE(store_rep1.representation(), MachineRepresentation::kMapWord);

#define STORE(kRep1, kRep2)                                          \
  static_assert(ElementSizeLog2Of(MachineRepresentation::kRep1) ==   \
                ElementSizeLog2Of(MachineRepresentation::kRep2));    \
  if (MachineRepresentation::kRep1 == store_rep1.representation() && \
      MachineRepresentation::kRep2 == store_rep2.representation()) { \
    if (store_rep1.write_barrier_kind() != kNoWriteBarrier ||        \
        store_rep2.write_barrier_kind() != kNoWriteBarrier) {        \
      return {};                                                     \
    }                                                                \
    return &cache_.k##StorePair##kRep1##kRep2##NoWriteBarrier;       \
  }
  STORE_PAIR_MACHINE_REPRESENTATION_LIST(STORE);
#undef STORE
  return {};
}

const Operator* MachineOperatorBuilder::ProtectedStore(
    MachineRepresentation rep) {
  switch (rep) {
#define STORE(kRep)                 \
  case MachineRepresentation::kRep: \
    return &cache_.kProtectedStore##kRep;
    MACHINE_REPRESENTATION_LIST(STORE)
#undef STORE
    case MachineRepresentation::kBit:
    case MachineRepresentation::kProtectedPointer:
    case MachineRepresentation::kIndirectPointer:
    case MachineRepresentation::kNone:
      UNREACHABLE();
  }
}

const Operator* MachineOperatorBuilder::StoreTrapOnNull(
    StoreRepresentation rep) {
  switch (rep.representation()) {
#define STORE(kRep)                                          \
  case MachineRepresentation::kRep:                          \
    if (rep.write_barrier_kind() == kNoWriteBarrier) {       \
      return &cache_.kStoreTrapOnNull##kRep##NoWriteBarrier; \
    }                                                        \
    DCHECK_EQ(kFullWriteBarrier, rep.write_barrier_kind());  \
    return &cache_.kStoreTrapOnNull##kRep##FullWriteBarrier;
    MACHINE_REPRESENTATION_LIST(STORE)
#undef STORE
    case MachineRepresentation::kBit:
    case MachineRepresentation::kProtectedPointer:
    case MachineRepresentation::kIndirectPointer:
    case MachineRepresentation::kNone:
      UNREACHABLE();
  }
}

const Operator* MachineOperatorBuilder::StackPointerGreaterThan(
    StackCheckKind kind) {
  switch (kind) {
    case StackCheckKind::kJSFunctionEntry:
      return &cache_.kStackPointerGreaterThanJSFunctionEntry;
    case StackCheckKind::kCodeStubAssembler:
      return &cache_.kStackPointerGreaterThanCodeStubAssembler;
    case StackCheckKind::kWasm:
      return &cache_.kStackPointerGreaterThanWasm;
    case StackCheckKind::kJSIterationBody:
      UNREACHABLE();
  }
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::BitcastWordToTagged() {
  return &cache_.kBitcastWordToTagged;
}

const Operator* MachineOperatorBuilder::BitcastTaggedToWord() {
  return &cache_.kBitcastTaggedToWord;
}

const Operator* MachineOperatorBuilder::BitcastMaybeObjectToWord() {
  return &cache_.kBitcastMaybeObjectToWord;
}

const Operator* MachineOperatorBuilder::AbortCSADcheck() {
  return &cache_.kAbortCSADcheck;
}

const Operator* MachineOperatorBuilder::DebugBreak() {
  return &cache_.kDebugBreak;
}

const Operator* MachineOperatorBuilder::Comment(const char* msg) {
  return zone_->New<CommentOperator>(msg);
}

const Operator* MachineOperatorBuilder::MemoryBarrier(AtomicMemoryOrder order) {
  switch (order) {
    case AtomicMemoryOrder::kSeqCst:
      return &cache_.kSeqCstMemoryBarrier;
    case AtomicMemoryOrder::kAcqRel:
      return &cache_.kAcqRelMemoryBarrier;
    default:
      UNREACHABLE();
  }
}

const Operator* MachineOperatorBuilder::Word32AtomicLoad(
    AtomicLoadParameters params) {
#define CACHED_LOAD_WITH_KIND(Type, Kind)               \
  if (params.representation() == MachineType::Type() && \
      params.order() == AtomicMemoryOrder::kSeqCst &&   \
      params.kind() == MemoryAccessKind::k##Kind) {     \
    return &cache_.kWord32SeqCstLoad##Type##Kind;       \
  }
#define CACHED_LOAD(Type)             \
  CACHED_LOAD_WITH_KIND(Type, Normal) \
  CACHED_LOAD_WITH_KIND(Type, Protected)
  ATOMIC_TYPE_LIST(CACHED_LOAD)
#undef CACHED_LOAD_WITH_KIND
#undef CACHED_LOAD

#define LOAD(Type)                                            \
  if (params.representation() == MachineType::Type()) {       \
    return zone_->New<Operator1<AtomicLoadParameters>>(       \
        IrOpcode::kWord32AtomicLoad, Operator::kNoProperties, \
        "Word32AtomicLoad", 2, 1, 1, 1, 1, 0, params);        \
  }
  ATOMIC_TYPE_LIST(LOAD)
  ATOMIC_TAGGED_TYPE_LIST(LOAD)
#undef LOAD

  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word32AtomicStore(
    AtomicStoreParameters params) {
#define CACHED_STORE_WITH_KIND(kRep, Kind)                      \
  if (params.representation() == MachineRepresentation::kRep && \
      params.order() == AtomicMemoryOrder::kSeqCst &&           \
      params.kind() == MemoryAccessKind::k##Kind) {             \
    return &cache_.kWord32SeqCstStore##kRep##Kind;              \
  }
#define CACHED_STORE(kRep)            \
  CACHED_STORE_WITH_KIND(kRep, Normal) \
  CACHED_STORE_WITH_KIND(kRep, Protected)
  ATOMIC_REPRESENTATION_LIST(CACHED_STORE)
#undef CACHED_STORE_WITH_KIND
#undef CACHED_STORE

#define STORE(kRep)                                                  \
  if (params.representation() == MachineRepresentation::kRep) {      \
    return zone_->New<Operator1<AtomicStoreParameters>>(             \
        IrOpcode::kWord32AtomicStore,                                \
        Operator::kNoDeopt | Operator::kNoRead | Operator::kNoThrow, \
        "Word32AtomicStore", 3, 1, 1, 0, 1, 0, params);              \
  }
  ATOMIC_REPRESENTATION_LIST(STORE)
  ATOMIC_TAGGED_REPRESENTATION_LIST(STORE)
#undef STORE
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word32AtomicExchange(
    AtomicOpParameters params) {
#define EXCHANGE_WITH_KIND(kType, Kind)                \
  if (params.type() == MachineType::kType()            \
      && params.kind() == MemoryAccessKind::k##Kind) { \
    return &cache_.kWord32AtomicExchange##kType##Kind; \
  }
#define EXCHANGE(kType) \
  EXCHANGE_WITH_KIND(kType, Normal) \
  EXCHANGE_WITH_KIND(kType, Protected)
  ATOMIC_TYPE_LIST(EXCHANGE)
#undef EXCHANGE_WITH_KIND
#undef EXCHANGE
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word32AtomicCompareExchange(
    AtomicOpParameters params) {
#define COMPARE_EXCHANGE_WITH_KIND(kType, Kind)               \
  if (params.type() == MachineType::kType()                   \
      && params.kind() == MemoryAccessKind::k##Kind) {        \
    return &cache_.kWord32AtomicCompareExchange##kType##Kind; \
  }
#define COMPARE_EXCHANGE(kType)             \
  COMPARE_EXCHANGE_WITH_KIND(kType, Normal) \
  COMPARE_EXCHANGE_WITH_KIND(kType, Protected)
  ATOMIC_TYPE_LIST(COMPARE_EXCHANGE)
#undef COMPARE_EXCHANGE_WITH_KIND
#undef COMPARE_EXCHANGE
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word32AtomicAdd(
    AtomicOpParameters params) {
#define OP_WITH_KIND(kType, Kind)                      \
  if (params.type() == MachineType::kType()            \
      && params.kind() == MemoryAccessKind::k##Kind) { \
    return &cache_.kWord32AtomicAdd##kType##Kind;      \
  }
#define OP(kType)             \
  OP_WITH_KIND(kType, Normal) \
  OP_WITH_KIND(kType, Protected)
  ATOMIC_TYPE_LIST(OP)
#undef OP_WITH_KIND
#undef OP
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word32AtomicSub(
    AtomicOpParameters params) {
#define OP_WITH_KIND(kType, Kind)                      \
  if (params.type() == MachineType::kType()            \
      && params.kind() == MemoryAccessKind::k##Kind) { \
    return &cache_.kWord32AtomicSub##kType##Kind;      \
  }
#define OP(kType)             \
  OP_WITH_KIND(kType, Normal) \
  OP_WITH_KIND(kType, Protected)
  ATOMIC_TYPE_LIST(OP)
#undef OP_WITH_KIND
#undef OP
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word32AtomicAnd(
    AtomicOpParameters params) {
#define OP_WITH_KIND(kType, Kind)                      \
  if (params.type() == MachineType::kType()            \
      && params.kind() == MemoryAccessKind::k##Kind) { \
    return &cache_.kWord32AtomicAnd##kType##Kind;      \
  }
#define OP(kType)             \
  OP_WITH_KIND(kType, Normal) \
  OP_WITH_KIND(kType, Protected)
  ATOMIC_TYPE_LIST(OP)
#undef OP_WITH_KIND
#undef OP
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word32AtomicOr(
    AtomicOpParameters params) {
#define OP_WITH_KIND(kType, Kind)                      \
  if (params.type() == MachineType::kType()            \
      && params.kind() == MemoryAccessKind::k##Kind) { \
    return &cache_.kWord32AtomicOr##kType##Kind;       \
  }
#define OP(kType)             \
  OP_WITH_KIND(kType, Normal) \
  OP_WITH_KIND(kType, Protected)
  ATOMIC_TYPE_LIST(OP)
#undef OP_WITH_KIND
#undef OP
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word32AtomicXor(
    AtomicOpParameters params) {
#define OP_WITH_KIND(kType, Kind)                      \
  if (params.type() == MachineType::kType()            \
      && params.kind() == MemoryAccessKind::k##Kind) { \
    return &cache_.kWord32AtomicXor##kType##Kind;      \
  }
#define OP(kType)             \
  OP_WITH_KIND(kType, Normal) \
  OP_WITH_KIND(kType, Protected)
  ATOMIC_TYPE_LIST(OP)
#undef OP_WITH_KIND
#undef OP
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word64AtomicLoad(
    AtomicLoadParameters params) {
#define CACHED_LOAD_WITH_KIND(Type, Kind)               \
  if (params.representation() == MachineType::Type() && \
      params.order() == AtomicMemoryOrder::kSeqCst &&   \
      params.kind() == MemoryAccessKind::k##Kind) {     \
    return &cache_.kWord64SeqCstLoad##Type##Kind;       \
  }
#define CACHED_LOAD(Type)             \
  CACHED_LOAD_WITH_KIND(Type, Normal) \
  CACHED_LOAD_WITH_KIND(Type, Protected)
  ATOMIC_U64_TYPE_LIST(CACHED_LOAD)
#undef CACHED_LOAD_WITH_KIND
#undef CACHED_LOAD

#define LOAD(Type)                                            \
  if (params.representation() == MachineType::Type()) {       \
    return zone_->New<Operator1<AtomicLoadParameters>>(       \
        IrOpcode::kWord64AtomicLoad, Operator::kNoProperties, \
        "Word64AtomicLoad", 2, 1, 1, 1, 1, 0, params);        \
  }
  ATOMIC_U64_TYPE_LIST(LOAD)
  ATOMIC64_TAGGED_TYPE_LIST(LOAD)
#undef LOAD

  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word64AtomicStore(
    AtomicStoreParameters params) {
#define CACHED_STORE_WITH_KIND(kRep, Kind)                      \
  if (params.representation() == MachineRepresentation::kRep && \
      params.order() == AtomicMemoryOrder::kSeqCst &&           \
      params.kind() == MemoryAccessKind::k##Kind) {             \
    return &cache_.kWord64SeqCstStore##kRep##Kind;              \
  }
#define CACHED_STORE(kRep)            \
  CACHED_STORE_WITH_KIND(kRep, Normal) \
  CACHED_STORE_WITH_KIND(kRep, Protected)
  ATOMIC64_REPRESENTATION_LIST(CACHED_STORE)
#undef CACHED_STORE_WITH_KIND
#undef CACHED_STORE

#define STORE(kRep)                                                  \
  if (params.representation() == MachineRepresentation::kRep) {      \
    return zone_->New<Operator1<AtomicStoreParameters>>(             \
        IrOpcode::kWord64AtomicStore,                                \
        Operator::kNoDeopt | Operator::kNoRead | Operator::kNoThrow, \
        "Word64AtomicStore", 3, 1, 1, 0, 1, 0, params);              \
  }
  ATOMIC64_REPRESENTATION_LIST(STORE)
  ATOMIC64_TAGGED_REPRESENTATION_LIST(STORE)
#undef STORE

  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word64AtomicAdd(
    AtomicOpParameters params) {
#define OP_WITH_KIND(kType, Kind)                      \
  if (params.type() == MachineType::kType()            \
      && params.kind() == MemoryAccessKind::k##Kind) { \
    return &cache_.kWord64AtomicAdd##kType##Kind;      \
  }
#define OP(kType)             \
  OP_WITH_KIND(kType, Normal) \
  OP_WITH_KIND(kType, Protected)
  ATOMIC_U64_TYPE_LIST(OP)
#undef OP_WITH_KIND
#undef OP
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word64AtomicSub(
    AtomicOpParameters params) {
#define OP_WITH_KIND(kType, Kind)                      \
  if (params.type() == MachineType::kType()            \
      && params.kind() == MemoryAccessKind::k##Kind) { \
    return &cache_.kWord64AtomicSub##kType##Kind;      \
  }
#define OP(kType)             \
  OP_WITH_KIND(kType, Normal) \
  OP_WITH_KIND(kType, Protected)
  ATOMIC_U64_TYPE_LIST(OP)
#undef OP_WITH_KIND
#undef OP
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word64AtomicAnd(
    AtomicOpParameters params) {
#define OP_WITH_KIND(kType, Kind)                      \
  if (params.type() == MachineType::kType()            \
      && params.kind() == MemoryAccessKind::k##Kind) { \
    return &cache_.kWord64AtomicAnd##kType##Kind;      \
  }
#define OP(kType)             \
  OP_WITH_KIND(kType, Normal) \
  OP_WITH_KIND(kType, Protected)
  ATOMIC_U64_TYPE_LIST(OP)
#undef OP_WITH_KIND
#undef OP
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word64AtomicOr(
    AtomicOpParameters params) {
#define OP_WITH_KIND(kType, Kind)                      \
  if (params.type() == MachineType::kType()            \
      && params.kind() == MemoryAccessKind::k##Kind) { \
    return &cache_.kWord64AtomicOr##kType##Kind;      \
  }
#define OP(kType)             \
  OP_WITH_KIND(kType, Normal) \
  OP_WITH_KIND(kType, Protected)
  ATOMIC_U64_TYPE_LIST(OP)
#undef OP_WITH_KIND
#undef OP
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word64AtomicXor(
    AtomicOpParameters params) {
#define OP_WITH_KIND(kType, Kind)                      \
  if (params.type() == MachineType::kType()            \
      && params.kind() == MemoryAccessKind::k##Kind) { \
    return &cache_.kWord64AtomicXor##kType##Kind;      \
  }
#define OP(kType)             \
  OP_WITH_KIND(kType, Normal) \
  OP_WITH_KIND(kType, Protected)
  ATOMIC_U64_TYPE_LIST(OP)
#undef OP_WITH_KIND
#undef OP
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word64AtomicExchange(
    AtomicOpParameters params) {
#define OP_WITH_KIND(kType, Kind)                           \
  if (params.type() == MachineType::kType()                 \
      && params.kind() == MemoryAccessKind::k##Kind) {      \
    return &cache_.kWord64AtomicExchange##kType##Kind;      \
  }
#define OP(kType)             \
  OP_WITH_KIND(kType, Normal) \
  OP_WITH_KIND(kType, Protected)
  ATOMIC_U64_TYPE_LIST(OP)
#undef OP_WITH_KIND
#undef OP
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word64AtomicCompareExchange(
    AtomicOpParameters params) {
#define OP_WITH_KIND(kType, Kind)                             \
  if (params.type() == MachineType::kType()                   \
      && params.kind() == MemoryAccessKind::k##Kind) {        \
    return &cache_.kWord64AtomicCompareExchange##kType##Kind; \
  }
#define OP(kType)             \
  OP_WITH_KIND(kType, Normal) \
  OP_WITH_KIND(kType, Protected)
  ATOMIC_U64_TYPE_LIST(OP)
#undef OP_WITH_KIND
#undef OP
  UNREACHABLE();
}

const Operator* MachineOperatorBuilder::Word32AtomicPairLoad(
    AtomicMemoryOrder order) {
  if (order == AtomicMemoryOrder::kSeqCst) {
    return &cache_.kWord32SeqCstPairLoad;
  }
  return zone_->New<Operator1<AtomicMemoryOrder>>(
      IrOpcode::kWord32AtomicPairLoad, Operator::kNoDeopt | Operator::kNoThrow,
      "Word32AtomicPairLoad", 2, 1, 1, 2, 1, 0, order);
}

const Operator* MachineOperatorBuilder::Word32AtomicPairStore(
    AtomicMemoryOrder order) {
  if (order == AtomicMemoryOrder::kSeqCst) {
    return &cache_.kWord32SeqCstPairStore;
  }
  return zone_->New<Operator1<AtomicMemoryOrder>>(
      IrOpcode::kWord32AtomicPairStore, Operator::kNoDeopt | Operator::kNoThrow,
      "Word32AtomicPairStore", 4, 1, 1, 0, 1, 0, order);
}

const Operator* MachineOperatorBuilder::Word32AtomicPairAdd() {
  return &cache_.kWord32AtomicPairAdd;
}

const Operator* MachineOperatorBuilder::Word32AtomicPairSub() {
  return &cache_.kWord32AtomicPairSub;
}

const Operator* MachineOperatorBuilder::Word32AtomicPairAnd() {
  return &cache_.kWord32AtomicPairAnd;
}

const Operator* MachineOperatorBuilder::Word32AtomicPairOr() {
  return &cache_.kWord32AtomicPairOr;
}

const Operator* MachineOperatorBuilder::Word32AtomicPairXor() {
  return &cache_.kWord32AtomicPairXor;
}

const Operator* MachineOperatorBuilder::Word32AtomicPairExchange() {
  return &cache_.kWord32AtomicPairExchange;
}

const Operator* MachineOperatorBuilder::Word32AtomicPairCompareExchange() {
  return &cache_.kWord32AtomicPairCompareExchange;
}

StackCheckKind StackCheckKindOf(Operator const* op) {
  DCHECK_EQ(IrOpcode::kStackPointerGreaterThan, op->opcode());
  return OpParameter<StackCheckKind>(op);
}

#if V8_ENABLE_WEBASSEMBLY
#define EXTRACT_LANE_OP(Type, Sign, lane_count)                      \
  const Operator* MachineOperatorBuilder::Type##ExtractLane##Sign(   \
      int32_t lane_index) {                                          \
    DCHECK(0 <= lane_index && lane_index < lane_count);              \
    return zone_->New<Operator1<int32_t>>(                           \
        IrOpcode::k##Type##ExtractLane##Sign, Operator::kPure,       \
        "" #Type "ExtractLane" #Sign, 1, 0, 0, 1, 0, 0, lane_index); \
  }
EXTRACT_LANE_OP(F64x2, , 2)
EXTRACT_LANE_OP(F32x4, , 4)
EXTRACT_LANE_OP(I64x2, , 2)
EXTRACT_LANE_OP(I32x4, , 4)
EXTRACT_LANE_OP(F16x8, , 8)
EXTRACT_LANE_OP(I16x8, U, 8)
EXTRACT_LANE_OP(I16x8, S, 8)
EXTRACT_LANE_OP(I8x16, U, 16)
EXTRACT_LANE_OP(I8x16, S, 16)
#undef EXTRACT_LANE_OP

#define REPLACE_LANE_OP(Type, lane_count)                                     \
  const Operator* MachineOperatorBuilder::Type##ReplaceLane(                  \
      int32_t lane_index) {                                                   \
    DCHECK(0 <= lane_index && lane_index < lane_count);                       \
    return zone_->New<Operator1<int32_t>>(IrOpcode::k##Type##ReplaceLane,     \
                                          Operator::kPure, "Replace lane", 2, \
                                          0, 0, 1, 0, 0, lane_index);         \
  }
SIMD_LANE_OP_LIST(REPLACE_LANE_OP)
#undef REPLACE_LANE_OP

const Operator* MachineOperatorBuilder::I64x2ReplaceLaneI32Pair(
    int32_t lane_index) {
  DCHECK(0 <= lane_index && lane_index < 2);
  return zone_->New<Operator1<int32_t>>(IrOpcode::kI64x2ReplaceLaneI32Pair,
                                        Operator::kPure, "Replace lane", 3, 0,
                                        0, 1, 0, 0, lane_index);
}

S128ImmediateParameter const& S128ImmediateParameterOf(Operator const* op) {
  DCHECK(IrOpcode::kI8x16Shuffle == op->opcode() ||
         IrOpcode::kS128Const == op->opcode());
  return OpParameter<S128ImmediateParameter>(op);
}

S256ImmediateParameter const& S256ImmediateParameterOf(Operator const* op) {
  DCHECK(IrOpcode::kI8x32Shuffle == op->opcode() ||
         IrOpcode::kS256Const == op->opcode());
  return OpParameter<S256ImmediateParameter>(op);
}

const Operator* MachineOperatorBuilder::S128Const(const uint8_t value[16]) {
  return zone_->New<Operator1<S128ImmediateParameter>>(
      IrOpcode::kS128Const, Operator::kPure, "Immediate", 0, 0, 0, 1, 0, 0,
      S128ImmediateParameter(value));
}

const Operator* MachineOperatorBuilder::S256Const(const uint8_t value[32]) {
  return zone_->New<Operator1<S256ImmediateParameter>>(
      IrOpcode::kS256Const, Operator::kPure, "Immediate256", 0, 0, 0, 1, 0, 0,
      S256ImmediateParameter(value));
}

const Operator* MachineOperatorBuilder::I8x16Shuffle(
    const uint8_t shuffle[16]) {
  return zone_->New<Operator1<S128ImmediateParameter>>(
      IrOpcode::kI8x16Shuffle, Operator::kPure, "I8x16Shuffle", 2, 0, 0, 1, 0,
      0, S128ImmediateParameter(shuffle));
}

const Operator* MachineOperatorBuilder::I8x16Swizzle(bool relaxed) {
  if (relaxed) {
    return &cache_.kI8x16RelaxedSwizzle;
  } else {
    return &cache_.kI8x16Swizzle;
  }
}

const Operator* MachineOperatorBuilder::I8x32Shuffle(
    const uint8_t shuffle[32]) {
  return zone_->New<Operator1<S256ImmediateParameter>>(
      IrOpcode::kI8x32Shuffle, Operator::kPure, "I8x32Shuffle", 2, 0, 0, 1, 0,
      0, S256ImmediateParameter(shuffle));
}

const Operator* MachineOperatorBuilder::ExtractF128(int32_t lane_index) {
  DCHECK(0 <= lane_index && lane_index < 2);
  class ExtractF128Operator final : public Operator1<int32_t> {
   public:
    explicit ExtractF128Operator(int32_t lane_index)
        : Operator1<int32_t>(IrOpcode::kExtractF128, Operator::kPure,
                             "ExtractF128", 1, 0, 0, 1, 0, 0, lane_index) {
      lane_index_ = lane_index;
    }

    int32_t lane_index_;
  };
  return zone_->New<ExtractF128Operator>(lane_index);
}

const Operator* MachineOperatorBuilder::LoadStackPointer() {
  class LoadStackPointerOperator final : public Operator {
   public:
    LoadStackPointerOperator()
        : Operator(IrOpcode::kLoadStackPointer, kNoProperties,
                   "LoadStackPointer", 0, 1, 0, 1, 1, 0) {}
  };
  return zone_->New<LoadStackPointerOperator>();
}

const Operator* MachineOperatorBuilder::SetStackPointer() {
  class SetStackPointerOperator final : public Operator {
   public:
    SetStackPointerOperator()
        : Operator(IrOpcode::kSetStackPointer, kNoProperties, "SetStackPointer",
                   1, 1, 0, 0, 1, 0) {}
  };
  return zone_->New<SetStackPointerOperator>();
}
#endif

#undef PURE_BINARY_OP_LIST_32
#undef PURE_BINARY_OP_LIST_64
#undef MACHINE_PURE_OP_LIST
#undef PURE_OPTIONAL_OP_LIST
#undef OVERFLOW_OP_LIST
#undef MACHINE_TYPE_LIST
#undef MACHINE_REPRESENTATION_LIST
#undef ATOMIC_TYPE_LIST
#undef ATOMIC_U64_TYPE_LIST
#undef ATOMIC_U32_TYPE_LIST
#undef ATOMIC_TAGGED_TYPE_LIST
#undef ATOMIC64_TAGGED_TYPE_LIST
#undef ATOMIC_REPRESENTATION_LIST
#undef ATOMIC_TAGGED_REPRESENTATION_LIST
#undef ATOMIC64_REPRESENTATION_LIST
#undef ATOMIC64_TAGGED_REPRESENTATION_LIST
#undef SIMD_LANE_OP_LIST
#undef STACK_SLOT_CACHED_SIZES_ALIGNMENTS_LIST
#undef LOAD_TRANSFORM_LIST

}  // namespace compiler
}  // namespace internal
}  // namespace v8
