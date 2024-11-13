// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/operations.h"

#include <atomic>
#include <iomanip>
#include <optional>
#include <sstream>

#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/codegen/bailout-reason.h"
#include "src/codegen/machine-type.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/frame-states.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/turboshaft/deopt-data.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/handles/handles-inl.h"
#include "src/handles/maybe-handles-inl.h"
#include "src/objects/code-inl.h"

#ifdef DEBUG
// For InWritableSharedSpace
#include "src/objects/objects-inl.h"
#endif

namespace v8::internal {
std::ostream& operator<<(std::ostream& os, AbortReason reason) {
  return os << GetAbortReason(reason);
}
}  // namespace v8::internal

namespace v8::internal::compiler::turboshaft {

void Operation::Print() const { std::cout << *this << "\n"; }

Zone* get_zone(Graph* graph) { return graph->graph_zone(); }

std::optional<Builtin> TryGetBuiltinId(const ConstantOp* target,
                                       JSHeapBroker* broker) {
  if (!target) return std::nullopt;
  if (target->kind != ConstantOp::Kind::kHeapObject) return std::nullopt;
  // TODO(nicohartmann@): For builtin compilation we don't have a broker. We
  // could try to access the heap directly instead.
  if (broker == nullptr) return std::nullopt;
  UnparkedScopeIfNeeded scope(broker);
  AllowHandleDereference allow_handle_dereference;
  HeapObjectRef ref = MakeRef(broker, target->handle());
  if (ref.IsCode()) {
    CodeRef code = ref.AsCode();
    if (code.object()->is_builtin()) {
      return code.object()->builtin_id();
    }
  }
  return std::nullopt;
}

bool CallOp::IsStackCheck(const Graph& graph, JSHeapBroker* broker,
                          StackCheckKind kind) const {
  auto builtin_id =
      TryGetBuiltinId(graph.Get(callee()).TryCast<ConstantOp>(), broker);
  if (!builtin_id.has_value()) return false;
  if (*builtin_id != Builtin::kCEntry_Return1_ArgvOnStack_NoBuiltinExit) {
    return false;
  }
  DCHECK_GE(input_count, 4);
  Runtime::FunctionId builtin = GetBuiltinForStackCheckKind(kind);
  auto is_this_builtin = [&](int input_index) {
    if (const ConstantOp* real_callee =
            graph.Get(input(input_index)).TryCast<ConstantOp>();
        real_callee != nullptr &&
        real_callee->kind == ConstantOp::Kind::kExternal &&
        real_callee->external_reference() ==
            ExternalReference::Create(builtin)) {
      return true;
    }
    return false;
  };
  // The function called by `CEntry_Return1_ArgvOnStack_NoBuiltinExit` is the
  // 3rd or the 4th argument of the CallOp (depending on the stack check kind),
  // so we check both of them.
  return is_this_builtin(2) || is_this_builtin(3);
}

void CallOp::PrintOptions(std::ostream& os) const {
  os << '[' << *descriptor->descriptor << ']';
}

void TailCallOp::PrintOptions(std::ostream& os) const {
  os << '[' << *descriptor->descriptor << ']';
}

#if DEBUG
bool ValidOpInputRep(
    const Graph& graph, OpIndex input,
    std::initializer_list<RegisterRepresentation> expected_reps,
    std::optional<size_t> projection_index) {
  base::Vector<const RegisterRepresentation> input_reps =
      graph.Get(input).outputs_rep();
  RegisterRepresentation input_rep;
  if (projection_index) {
    if (*projection_index < input_reps.size()) {
      input_rep = input_reps[*projection_index];
    } else {
      std::cerr << "Turboshaft operation has input #" << input
                << " with wrong arity.\n";
      std::cerr << "Input has results " << PrintCollection(input_reps)
                << ", but expected at least " << (*projection_index + 1)
                << " results.\n";
      return false;
    }
  } else if (input_reps.size() == 1) {
    input_rep = input_reps[0];
  } else {
    std::cerr << "Turboshaft operation has input #" << input
              << " with wrong arity.\n";
    std::cerr << "Expected a single output but found " << input_reps.size()
              << ".\n";
    return false;
  }
  for (RegisterRepresentation expected_rep : expected_reps) {
    if (input_rep.AllowImplicitRepresentationChangeTo(
            expected_rep, graph.IsCreatedFromTurbofan())) {
      return true;
    }
  }
  std::cerr << "Turboshaft operation has input #" << input
            << " with wrong representation.\n";
  std::cerr << "Expected " << (expected_reps.size() > 1 ? "one of " : "")
            << PrintCollection(expected_reps).WithoutBrackets() << " but found "
            << input_rep << ".\n";
  return false;
}

bool ValidOpInputRep(const Graph& graph, OpIndex input,
                     RegisterRepresentation expected_rep,
                     std::optional<size_t> projection_index) {
  return ValidOpInputRep(graph, input, {expected_rep}, projection_index);
}
#endif  // DEBUG

const char* OpcodeName(Opcode opcode) {
#define OPCODE_NAME(Name) #Name,
  const char* table[kNumberOfOpcodes] = {
      TURBOSHAFT_OPERATION_LIST(OPCODE_NAME)};
#undef OPCODE_NAME
  return table[OpcodeIndex(opcode)];
}

std::ostream& operator<<(std::ostream& os, OperationPrintStyle styled_op) {
  const Operation& op = styled_op.op;
  os << OpcodeName(op.opcode);
  op.PrintInputs(os, styled_op.op_index_prefix);
  op.PrintOptions(os);
  return os;
}

std::ostream& operator<<(std::ostream& os, GenericBinopOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(Name)              \
  case GenericBinopOp::Kind::k##Name: \
    return os << #Name;
    GENERIC_BINOP_LIST(PRINT_KIND)
#undef PRINT_KIND
  }
}

std::ostream& operator<<(std::ostream& os, GenericUnopOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(Name)             \
  case GenericUnopOp::Kind::k##Name: \
    return os << #Name;
    GENERIC_UNOP_LIST(PRINT_KIND)
#undef PRINT_KIND
  }
}

std::ostream& operator<<(std::ostream& os, WordUnaryOp::Kind kind) {
  switch (kind) {
    case WordUnaryOp::Kind::kReverseBytes:
      return os << "ReverseBytes";
    case WordUnaryOp::Kind::kCountLeadingZeros:
      return os << "CountLeadingZeros";
    case WordUnaryOp::Kind::kCountTrailingZeros:
      return os << "CountTrailingZeros";
    case WordUnaryOp::Kind::kPopCount:
      return os << "PopCount";
    case WordUnaryOp::Kind::kSignExtend8:
      return os << "SignExtend8";
    case WordUnaryOp::Kind::kSignExtend16:
      return os << "SignExtend16";
  }
}

std::ostream& operator<<(std::ostream& os, OverflowCheckedUnaryOp::Kind kind) {
  switch (kind) {
    case OverflowCheckedUnaryOp::Kind::kAbs:
      return os << "kAbs";
  }
}

std::ostream& operator<<(std::ostream& os, FloatUnaryOp::Kind kind) {
  switch (kind) {
    case FloatUnaryOp::Kind::kAbs:
      return os << "Abs";
    case FloatUnaryOp::Kind::kNegate:
      return os << "Negate";
    case FloatUnaryOp::Kind::kSilenceNaN:
      return os << "SilenceNaN";
    case FloatUnaryOp::Kind::kRoundUp:
      return os << "RoundUp";
    case FloatUnaryOp::Kind::kRoundDown:
      return os << "RoundDown";
    case FloatUnaryOp::Kind::kRoundToZero:
      return os << "RoundToZero";
    case FloatUnaryOp::Kind::kRoundTiesEven:
      return os << "RoundTiesEven";
    case FloatUnaryOp::Kind::kLog:
      return os << "Log";
    case FloatUnaryOp::Kind::kLog2:
      return os << "Log2";
    case FloatUnaryOp::Kind::kLog10:
      return os << "Log10";
    case FloatUnaryOp::Kind::kLog1p:
      return os << "Log1p";
    case FloatUnaryOp::Kind::kSqrt:
      return os << "Sqrt";
    case FloatUnaryOp::Kind::kCbrt:
      return os << "Cbrt";
    case FloatUnaryOp::Kind::kExp:
      return os << "Exp";
    case FloatUnaryOp::Kind::kExpm1:
      return os << "Expm1";
    case FloatUnaryOp::Kind::kSin:
      return os << "Sin";
    case FloatUnaryOp::Kind::kCos:
      return os << "Cos";
    case FloatUnaryOp::Kind::kAsin:
      return os << "Asin";
    case FloatUnaryOp::Kind::kAcos:
      return os << "Acos";
    case FloatUnaryOp::Kind::kSinh:
      return os << "Sinh";
    case FloatUnaryOp::Kind::kCosh:
      return os << "Cosh";
    case FloatUnaryOp::Kind::kAsinh:
      return os << "Asinh";
    case FloatUnaryOp::Kind::kAcosh:
      return os << "Acosh";
    case FloatUnaryOp::Kind::kTan:
      return os << "Tan";
    case FloatUnaryOp::Kind::kTanh:
      return os << "Tanh";
    case FloatUnaryOp::Kind::kAtan:
      return os << "Atan";
    case FloatUnaryOp::Kind::kAtanh:
      return os << "Atanh";
  }
}

// static
bool FloatUnaryOp::IsSupported(Kind kind, FloatRepresentation rep) {
  switch (rep.value()) {
    case FloatRepresentation::Float32():
      switch (kind) {
        case Kind::kRoundDown:
          return SupportedOperations::float32_round_down();
        case Kind::kRoundUp:
          return SupportedOperations::float32_round_up();
        case Kind::kRoundToZero:
          return SupportedOperations::float32_round_to_zero();
        case Kind::kRoundTiesEven:
          return SupportedOperations::float32_round_ties_even();
        default:
          return true;
      }
    case FloatRepresentation::Float64():
      switch (kind) {
        case Kind::kRoundDown:
          return SupportedOperations::float64_round_down();
        case Kind::kRoundUp:
          return SupportedOperations::float64_round_up();
        case Kind::kRoundToZero:
          return SupportedOperations::float64_round_to_zero();
        case Kind::kRoundTiesEven:
          return SupportedOperations::float64_round_ties_even();
        default:
          return true;
      }
  }
}

// static
bool WordUnaryOp::IsSupported(Kind kind, WordRepresentation rep) {
  switch (kind) {
    case Kind::kCountLeadingZeros:
    case Kind::kReverseBytes:
    case Kind::kSignExtend8:
    case Kind::kSignExtend16:
      return true;
    case Kind::kCountTrailingZeros:
      return rep == WordRepresentation::Word32()
                 ? SupportedOperations::word32_ctz()
                 : SupportedOperations::word64_ctz();
    case Kind::kPopCount:
      return rep == WordRepresentation::Word32()
                 ? SupportedOperations::word32_popcnt()
                 : SupportedOperations::word64_popcnt();
  }
}

std::ostream& operator<<(std::ostream& os, ShiftOp::Kind kind) {
  switch (kind) {
    case ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros:
      return os << "ShiftRightArithmeticShiftOutZeros";
    case ShiftOp::Kind::kShiftRightArithmetic:
      return os << "ShiftRightArithmetic";
    case ShiftOp::Kind::kShiftRightLogical:
      return os << "ShiftRightLogical";
    case ShiftOp::Kind::kShiftLeft:
      return os << "ShiftLeft";
    case ShiftOp::Kind::kRotateRight:
      return os << "RotateRight";
    case ShiftOp::Kind::kRotateLeft:
      return os << "RotateLeft";
  }
}

std::ostream& operator<<(std::ostream& os, ComparisonOp::Kind kind) {
  switch (kind) {
    case ComparisonOp::Kind::kEqual:
      return os << "Equal";
    case ComparisonOp::Kind::kSignedLessThan:
      return os << "SignedLessThan";
    case ComparisonOp::Kind::kSignedLessThanOrEqual:
      return os << "SignedLessThanOrEqual";
    case ComparisonOp::Kind::kUnsignedLessThan:
      return os << "UnsignedLessThan";
    case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
      return os << "UnsignedLessThanOrEqual";
  }
}

std::ostream& operator<<(std::ostream& os, ChangeOp::Kind kind) {
  switch (kind) {
    case ChangeOp::Kind::kFloatConversion:
      return os << "FloatConversion";
    case ChangeOp::Kind::kJSFloatTruncate:
      return os << "JSFloatTruncate";
    case ChangeOp::Kind::kSignedFloatTruncateOverflowToMin:
      return os << "SignedFloatTruncateOverflowToMin";
    case ChangeOp::Kind::kUnsignedFloatTruncateOverflowToMin:
      return os << "UnsignedFloatTruncateOverflowToMin";
    case ChangeOp::Kind::kSignedToFloat:
      return os << "SignedToFloat";
    case ChangeOp::Kind::kUnsignedToFloat:
      return os << "UnsignedToFloat";
    case ChangeOp::Kind::kExtractHighHalf:
      return os << "ExtractHighHalf";
    case ChangeOp::Kind::kExtractLowHalf:
      return os << "ExtractLowHalf";
    case ChangeOp::Kind::kZeroExtend:
      return os << "ZeroExtend";
    case ChangeOp::Kind::kSignExtend:
      return os << "SignExtend";
    case ChangeOp::Kind::kTruncate:
      return os << "Truncate";
    case ChangeOp::Kind::kBitcast:
      return os << "Bitcast";
  }
}

std::ostream& operator<<(std::ostream& os, ChangeOrDeoptOp::Kind kind) {
  switch (kind) {
    case ChangeOrDeoptOp::Kind::kUint32ToInt32:
      return os << "Uint32ToInt32";
    case ChangeOrDeoptOp::Kind::kInt64ToInt32:
      return os << "Int64ToInt32";
    case ChangeOrDeoptOp::Kind::kUint64ToInt32:
      return os << "Uint64ToInt32";
    case ChangeOrDeoptOp::Kind::kUint64ToInt64:
      return os << "Uint64ToInt64";
    case ChangeOrDeoptOp::Kind::kFloat64ToInt32:
      return os << "Float64ToInt32";
    case ChangeOrDeoptOp::Kind::kFloat64ToUint32:
      return os << "Float64ToUint32";
    case ChangeOrDeoptOp::Kind::kFloat64ToInt64:
      return os << "Float64ToInt64";
    case ChangeOrDeoptOp::Kind::kFloat64NotHole:
      return os << "Float64NotHole";
  }
}

std::ostream& operator<<(std::ostream& os, TryChangeOp::Kind kind) {
  switch (kind) {
    case TryChangeOp::Kind::kSignedFloatTruncateOverflowUndefined:
      return os << "SignedFloatTruncateOverflowUndefined";
    case TryChangeOp::Kind::kUnsignedFloatTruncateOverflowUndefined:
      return os << "UnsignedFloatTruncateOverflowUndefined";
  }
}

std::ostream& operator<<(std::ostream& os, TaggedBitcastOp::Kind kind) {
  switch (kind) {
    case TaggedBitcastOp::Kind::kSmi:
      return os << "Smi";
    case TaggedBitcastOp::Kind::kHeapObject:
      return os << "HeapObject";
    case TaggedBitcastOp::Kind::kTagAndSmiBits:
      return os << "TagAndSmiBits";
    case TaggedBitcastOp::Kind::kAny:
      return os << "Any";
  }
}

std::ostream& operator<<(std::ostream& os, ChangeOp::Assumption assumption) {
  switch (assumption) {
    case ChangeOp::Assumption::kNoAssumption:
      return os << "NoAssumption";
    case ChangeOp::Assumption::kNoOverflow:
      return os << "NoOverflow";
    case ChangeOp::Assumption::kReversible:
      return os << "Reversible";
  }
}

std::ostream& operator<<(std::ostream& os, SelectOp::Implementation kind) {
  switch (kind) {
    case SelectOp::Implementation::kBranch:
      return os << "Branch";
    case SelectOp::Implementation::kCMove:
      return os << "CMove";
  }
}

std::ostream& operator<<(std::ostream& os, AtomicRMWOp::BinOp bin_op) {
  switch (bin_op) {
    case AtomicRMWOp::BinOp::kAdd:
      return os << "add";
    case AtomicRMWOp::BinOp::kSub:
      return os << "sub";
    case AtomicRMWOp::BinOp::kAnd:
      return os << "and";
    case AtomicRMWOp::BinOp::kOr:
      return os << "or";
    case AtomicRMWOp::BinOp::kXor:
      return os << "xor";
    case AtomicRMWOp::BinOp::kExchange:
      return os << "exchange";
    case AtomicRMWOp::BinOp::kCompareExchange:
      return os << "compare-exchange";
  }
}

std::ostream& operator<<(std::ostream& os, AtomicWord32PairOp::Kind bin_op) {
  switch (bin_op) {
    case AtomicWord32PairOp::Kind::kAdd:
      return os << "add";
    case AtomicWord32PairOp::Kind::kSub:
      return os << "sub";
    case AtomicWord32PairOp::Kind::kAnd:
      return os << "and";
    case AtomicWord32PairOp::Kind::kOr:
      return os << "or";
    case AtomicWord32PairOp::Kind::kXor:
      return os << "xor";
    case AtomicWord32PairOp::Kind::kExchange:
      return os << "exchange";
    case AtomicWord32PairOp::Kind::kCompareExchange:
      return os << "compare-exchange";
    case AtomicWord32PairOp::Kind::kLoad:
      return os << "load";
    case AtomicWord32PairOp::Kind::kStore:
      return os << "store";
  }
}

std::ostream& operator<<(std::ostream& os, FrameConstantOp::Kind kind) {
  switch (kind) {
    case FrameConstantOp::Kind::kStackCheckOffset:
      return os << "stack check offset";
    case FrameConstantOp::Kind::kFramePointer:
      return os << "frame pointer";
    case FrameConstantOp::Kind::kParentFramePointer:
      return os << "parent frame pointer";
  }
}

void Operation::PrintInputs(std::ostream& os,
                            const std::string& op_index_prefix) const {
  switch (opcode) {
#define SWITCH_CASE(Name)                              \
  case Opcode::k##Name:                                \
    Cast<Name##Op>().PrintInputs(os, op_index_prefix); \
    break;
    TURBOSHAFT_OPERATION_LIST(SWITCH_CASE)
#undef SWITCH_CASE
  }
}

void Operation::PrintOptions(std::ostream& os) const {
  switch (opcode) {
#define SWITCH_CASE(Name)              \
  case Opcode::k##Name:                \
    Cast<Name##Op>().PrintOptions(os); \
    break;
    TURBOSHAFT_OPERATION_LIST(SWITCH_CASE)
#undef SWITCH_CASE
  }
}

void ConstantOp::PrintOptions(std::ostream& os) const {
  os << '[';
  switch (kind) {
    case Kind::kWord32:
      os << "word32: " << static_cast<int32_t>(storage.integral);
      break;
    case Kind::kWord64:
      os << "word64: " << static_cast<int64_t>(storage.integral);
      break;
    case Kind::kSmi:
      os << "smi: " << smi();
      break;
    case Kind::kNumber:
      os << "number: " << number().get_scalar();
      break;
    case Kind::kTaggedIndex:
      os << "tagged index: " << tagged_index();
      break;
    case Kind::kFloat64:
      os << "float64: " << float64().get_scalar();
      if (float64().is_hole_nan()) {
        os << " (hole nan: 0x" << std::hex << float64().get_bits() << std::dec
           << ')';
      } else if (float64().is_nan()) {
        os << " (0x" << std::hex << float64().get_bits() << std::dec << ')';
      }
      break;
    case Kind::kFloat32:
      os << "float32: " << float32().get_scalar();
      if (float32().is_nan()) {
        os << " (0x" << std::hex << base::bit_cast<uint32_t>(storage.float32)
           << std::dec << ')';
      }
      break;
    case Kind::kExternal:
      os << "external: " << external_reference();
      break;
    case Kind::kHeapObject:
      os << "heap object: " << JSONEscaped(handle());
      break;
    case Kind::kCompressedHeapObject:
      os << "compressed heap object: " << JSONEscaped(handle());
      break;
    case Kind::kTrustedHeapObject:
      os << "trusted heap object: " << JSONEscaped(handle());
      break;
    case Kind::kRelocatableWasmCall:
      os << "relocatable wasm call: 0x"
         << reinterpret_cast<void*>(storage.integral);
      break;
    case Kind::kRelocatableWasmStubCall:
      os << "relocatable wasm stub call: 0x"
         << reinterpret_cast<void*>(storage.integral);
      break;
    case Kind::kRelocatableWasmCanonicalSignatureId:
      os << "relocatable wasm canonical signature ID: "
         << static_cast<int32_t>(storage.integral);
      break;
  }
  os << ']';
}

void ParameterOp::PrintOptions(std::ostream& os) const {
  os << '[' << parameter_index;
  if (debug_name) os << ", " << debug_name;
  os << ']';
}

MachineType LoadOp::machine_type() const {
  if (result_rep == RegisterRepresentation::Compressed()) {
    if (loaded_rep == MemoryRepresentation::AnyTagged()) {
      return MachineType::AnyCompressed();
    } else if (loaded_rep == MemoryRepresentation::TaggedPointer()) {
      return MachineType::CompressedPointer();
    }
  }
  return loaded_rep.ToMachineType();
}

void LoadOp::PrintInputs(std::ostream& os,
                         const std::string& op_index_prefix) const {
  os << " *(" << op_index_prefix << base().id();
  if (offset < 0) {
    os << " - " << -offset;
  } else if (offset > 0) {
    os << " + " << offset;
  }
  if (index().valid()) {
    os << " + " << op_index_prefix << index().value().id();
    if (element_size_log2 > 0) os << '*' << (1 << element_size_log2);
  }
  os << ") ";
}
void LoadOp::PrintOptions(std::ostream& os) const {
  os << '[';
  os << (kind.tagged_base ? "tagged base" : "raw");
  if (kind.maybe_unaligned) os << ", unaligned";
  if (kind.with_trap_handler) os << ", protected";
  os << ", " << loaded_rep;
  os << ", " << result_rep;
  if (element_size_log2 != 0)
    os << ", element size: 2^" << int{element_size_log2};
  if (offset != 0) os << ", offset: " << offset;
  os << ']';
}

void AtomicRMWOp::PrintInputs(std::ostream& os,
                              const std::string& op_index_prefix) const {
  os << " *(" << op_index_prefix << base().id() << " + " << op_index_prefix
     << index().id() << ").atomic_" << bin_op << '(';
  if (bin_op == BinOp::kCompareExchange) {
    os << "expected: " << op_index_prefix << expected();
    os << ", new: " << op_index_prefix << value();
  } else {
    os << op_index_prefix << value().id();
  }
  os << ')';
}

void AtomicRMWOp::PrintOptions(std::ostream& os) const {
  os << '[' << "binop: " << bin_op << ", in_out_rep: " << in_out_rep
     << ", memory_rep: " << memory_rep << ']';
}

void AtomicWord32PairOp::PrintInputs(std::ostream& os,
                                     const std::string& op_index_prefix) const {
  os << " *(" << op_index_prefix << base().id();
  if (index().valid()) {
    os << " + " << op_index_prefix << index().value().id();
  }
  if (offset) {
    os << " + offset=" << offset;
  }
  os << ").atomic_word32_pair_" << kind << '(';
  if (kind == Kind::kCompareExchange) {
    os << "expected: {lo: " << op_index_prefix << value_low()
       << ", hi: " << op_index_prefix << value_high();
    os << "}, value: {lo: " << op_index_prefix << value_low()
       << ", hi: " << op_index_prefix << value_high() << '}';
  } else if (kind != Kind::kLoad) {
    os << "lo: " << op_index_prefix << value_low()
       << ", hi: " << op_index_prefix << value_high();
  }
  os << ')';
}

void AtomicWord32PairOp::PrintOptions(std::ostream& os) const {
  os << "[opkind: " << kind << ']';
}

void MemoryBarrierOp::PrintOptions(std::ostream& os) const {
  os << "[memory order: " << memory_order << ']';
}

void StoreOp::PrintInputs(std::ostream& os,
                          const std::string& op_index_prefix) const {
  os << " *(" << op_index_prefix << base().id();
  if (offset < 0) {
    os << " - " << -offset;
  } else if (offset > 0) {
    os << " + " << offset;
  }
  if (index().valid()) {
    os << " + " << op_index_prefix << index().value().id();
    if (element_size_log2 > 0) os << '*' << (1 << element_size_log2);
  }
  os << ") = " << op_index_prefix << value().id() << ' ';
}
void StoreOp::PrintOptions(std::ostream& os) const {
  os << '[';
  os << (kind.tagged_base ? "tagged base" : "raw");
  if (kind.maybe_unaligned) os << ", unaligned";
  if (kind.with_trap_handler) os << ", protected";
  os << ", " << stored_rep;
  os << ", " << write_barrier;
  if (element_size_log2 != 0)
    os << ", element size: 2^" << int{element_size_log2};
  if (offset != 0) os << ", offset: " << offset;
  if (maybe_initializing_or_transitioning) os << ", initializing";
  os << ']';
}

void AllocateOp::PrintOptions(std::ostream& os) const {
  os << '[';
  os << type;
  os << ']';
}

void DecodeExternalPointerOp::PrintOptions(std::ostream& os) const {
  os << '[';
  os << "tag: " << std::hex << tag << std::dec;
  os << ']';
}

void FrameStateOp::PrintOptions(std::ostream& os) const {
  os << '[';
  os << (inlined ? "inlined" : "not inlined");
  os << ", ";
  os << data->frame_state_info;
  os << ", state values:";
  FrameStateData::Iterator it = data->iterator(state_values());
  while (it.has_more()) {
    os << ' ';
    switch (it.current_instr()) {
      case FrameStateData::Instr::kInput: {
        MachineType type;
        OpIndex input;
        it.ConsumeInput(&type, &input);
        os << '#' << input.id() << '(' << type << ')';
        break;
      }
      case FrameStateData::Instr::kUnusedRegister:
        it.ConsumeUnusedRegister();
        os << '.';
        break;
      case FrameStateData::Instr::kDematerializedObject: {
        uint32_t id;
        uint32_t field_count;
        it.ConsumeDematerializedObject(&id, &field_count);
        os << '$' << id << "(field count: " << field_count << ')';
        break;
      }
      case FrameStateData::Instr::kDematerializedObjectReference: {
        uint32_t id;
        it.ConsumeDematerializedObjectReference(&id);
        os << '$' << id;
        break;
      }
      case FrameStateData::Instr::kArgumentsElements: {
        CreateArgumentsType type;
        it.ConsumeArgumentsElements(&type);
        os << "ArgumentsElements(" << type << ')';
        break;
      }
      case FrameStateData::Instr::kArgumentsLength: {
        it.ConsumeArgumentsLength();
        os << "ArgumentsLength";
        break;
      }
      case FrameStateData::Instr::kRestLength: {
        it.ConsumeRestLength();
        os << "RestLength";
        break;
      }
    }
  }
  os << ']';
}

void FrameStateOp::Validate(const Graph& graph) const {
  if (inlined) {
    DCHECK(Get(graph, parent_frame_state()).Is<FrameStateOp>());
  }
  FrameStateData::Iterator it = data->iterator(state_values());
  while (it.has_more()) {
    switch (it.current_instr()) {
      case FrameStateData::Instr::kInput: {
        MachineType type;
        OpIndex input;
        it.ConsumeInput(&type, &input);
        RegisterRepresentation rep =
            RegisterRepresentation::FromMachineRepresentation(
                type.representation());
        if (rep == RegisterRepresentation::Tagged()) {
          // The deoptimizer can handle compressed values.
          rep = RegisterRepresentation::Compressed();
        }
        DCHECK(ValidOpInputRep(graph, input, rep));
        break;
      }
      case FrameStateData::Instr::kUnusedRegister:
        it.ConsumeUnusedRegister();
        break;
      case FrameStateData::Instr::kDematerializedObject: {
        uint32_t id;
        uint32_t field_count;
        it.ConsumeDematerializedObject(&id, &field_count);
        break;
      }
      case FrameStateData::Instr::kDematerializedObjectReference: {
        uint32_t id;
        it.ConsumeDematerializedObjectReference(&id);
        break;
      }
      case FrameStateData::Instr::kArgumentsElements: {
        CreateArgumentsType type;
        it.ConsumeArgumentsElements(&type);
        break;
      }
      case FrameStateData::Instr::kArgumentsLength: {
        it.ConsumeArgumentsLength();
        break;
      }
      case FrameStateData::Instr::kRestLength: {
        it.ConsumeRestLength();
        break;
      }
    }
  }
}

void DeoptimizeIfOp::PrintOptions(std::ostream& os) const {
  static_assert(std::tuple_size_v<decltype(options())> == 2);
  os << '[' << (negated ? "negated, " : "") << *parameters << ']';
}

void DidntThrowOp::Validate(const Graph& graph) const {
#ifdef DEBUG
  DCHECK(MayThrow(graph.Get(throwing_operation()).opcode));
  switch (graph.Get(throwing_operation()).opcode) {
    case Opcode::kCall: {
      auto& call_op = graph.Get(throwing_operation()).Cast<CallOp>();
      DCHECK_EQ(call_op.descriptor->out_reps, outputs_rep());
      break;
    }
#define STATIC_OUTPUT_CASE(Name)                                           \
  case Opcode::k##Name: {                                                  \
    const Name##Op& op = graph.Get(throwing_operation()).Cast<Name##Op>(); \
    DCHECK_EQ(op.kOutReps, outputs_rep());                                 \
    break;                                                                 \
  }
      TURBOSHAFT_THROWING_STATIC_OUTPUTS_OPERATIONS_LIST(STATIC_OUTPUT_CASE)
#undef STATIC_OUTPUT_CASE
    default:
      UNREACHABLE();
  }
  // Check that `may_throw()` is either immediately before or that there is only
  // a `CheckExceptionOp` in-between.
  OpIndex this_index = graph.Index(*this);
  OpIndex in_between = graph.NextIndex(throwing_operation());
  if (has_catch_block) {
    DCHECK_NE(in_between, this_index);
    auto& catch_op = graph.Get(in_between).Cast<CheckExceptionOp>();
    DCHECK_EQ(catch_op.didnt_throw_block->begin(), this_index);
  } else {
    DCHECK_EQ(in_between, this_index);
  }
#endif
}

void WordBinopOp::PrintOptions(std::ostream& os) const {
  os << '[';
  switch (kind) {
    case Kind::kAdd:
      os << "Add, ";
      break;
    case Kind::kSub:
      os << "Sub, ";
      break;
    case Kind::kMul:
      os << "Mul, ";
      break;
    case Kind::kSignedMulOverflownBits:
      os << "SignedMulOverflownBits, ";
      break;
    case Kind::kUnsignedMulOverflownBits:
      os << "UnsignedMulOverflownBits, ";
      break;
    case Kind::kSignedDiv:
      os << "SignedDiv, ";
      break;
    case Kind::kUnsignedDiv:
      os << "UnsignedDiv, ";
      break;
    case Kind::kSignedMod:
      os << "SignedMod, ";
      break;
    case Kind::kUnsignedMod:
      os << "UnsignedMod, ";
      break;
    case Kind::kBitwiseAnd:
      os << "BitwiseAnd, ";
      break;
    case Kind::kBitwiseOr:
      os << "BitwiseOr, ";
      break;
    case Kind::kBitwiseXor:
      os << "BitwiseXor, ";
      break;
  }
  os << rep;
  os << ']';
}

void FloatBinopOp::PrintOptions(std::ostream& os) const {
  os << '[';
  switch (kind) {
    case Kind::kAdd:
      os << "Add, ";
      break;
    case Kind::kSub:
      os << "Sub, ";
      break;
    case Kind::kMul:
      os << "Mul, ";
      break;
    case Kind::kDiv:
      os << "Div, ";
      break;
    case Kind::kMod:
      os << "Mod, ";
      break;
    case Kind::kMin:
      os << "Min, ";
      break;
    case Kind::kMax:
      os << "Max, ";
      break;
    case Kind::kPower:
      os << "Power, ";
      break;
    case Kind::kAtan2:
      os << "Atan2, ";
      break;
  }
  os << rep;
  os << ']';
}

void Word32PairBinopOp::PrintOptions(std::ostream& os) const {
  os << '[';
  switch (kind) {
    case Kind::kAdd:
      os << "Add";
      break;
    case Kind::kSub:
      os << "Sub";
      break;
    case Kind::kMul:
      os << "Mul";
      break;
    case Kind::kShiftLeft:
      os << "ShiftLeft";
      break;
    case Kind::kShiftRightArithmetic:
      os << "ShiftRightSigned";
      break;
    case Kind::kShiftRightLogical:
      os << "ShiftRightUnsigned";
      break;
  }
  os << ']';
}

void WordBinopDeoptOnOverflowOp::PrintOptions(std::ostream& os) const {
  os << '[';
  switch (kind) {
    case Kind::kSignedAdd:
      os << "signed add, ";
      break;
    case Kind::kSignedMul:
      os << "signed mul, ";
      break;
    case Kind::kSignedSub:
      os << "signed sub, ";
      break;
    case Kind::kSignedDiv:
      os << "signed div, ";
      break;
    case Kind::kSignedMod:
      os << "signed mod, ";
      break;
    case Kind::kUnsignedDiv:
      os << "unsigned div, ";
      break;
    case Kind::kUnsignedMod:
      os << "unsigned mod, ";
      break;
  }
  os << rep << ", " << mode;
  os << ']';
}

void OverflowCheckedBinopOp::PrintOptions(std::ostream& os) const {
  os << '[';
  switch (kind) {
    case Kind::kSignedAdd:
      os << "signed add, ";
      break;
    case Kind::kSignedSub:
      os << "signed sub, ";
      break;
    case Kind::kSignedMul:
      os << "signed mul, ";
      break;
  }
  os << rep;
  os << ']';
}

std::ostream& operator<<(std::ostream& os, OpIndex idx) {
  if (!idx.valid()) {
    return os << "<invalid OpIndex>";
  }
  return os << idx.id();
}

std::ostream& operator<<(std::ostream& os, BlockIndex b) {
  if (!b.valid()) {
    return os << "<invalid block>";
  }
  return os << 'B' << b.id();
}

std::ostream& operator<<(std::ostream& os, const Block* b) {
  return os << b->index();
}

std::ostream& operator<<(std::ostream& os, OpEffects effects) {
  auto produce_consume = [](bool produces, bool consumes) {
    if (!produces && !consumes) {
      return "🁣";
    } else if (produces && !consumes) {
      return "🁤";
    } else if (!produces && consumes) {
      return "🁪";
    } else if (produces && consumes) {
      return "🁫";
    }
    UNREACHABLE();
  };
  os << produce_consume(effects.produces.load_heap_memory,
                        effects.consumes.load_heap_memory);
  os << produce_consume(effects.produces.load_off_heap_memory,
                        effects.consumes.load_off_heap_memory);
  os << "\u2003";  // em space
  os << produce_consume(effects.produces.store_heap_memory,
                        effects.consumes.store_heap_memory);
  os << produce_consume(effects.produces.store_off_heap_memory,
                        effects.consumes.store_off_heap_memory);
  os << "\u2003";  // em space
  os << produce_consume(effects.produces.before_raw_heap_access,
                        effects.consumes.before_raw_heap_access);
  os << produce_consume(effects.produces.after_raw_heap_access,
                        effects.consumes.after_raw_heap_access);
  os << "\u2003";  // em space
  os << produce_consume(effects.produces.control_flow,
                        effects.consumes.control_flow);
  os << "\u2003";  // em space
  os << (effects.can_create_identity ? 'i' : '_');
  os << ' ' << (effects.can_allocate ? 'a' : '_');
  return os;
}

void SwitchOp::PrintOptions(std::ostream& os) const {
  os << '[';
  for (const Case& c : cases) {
    os << "case " << c.value << ": " << c.destination << ", ";
  }
  os << " default: " << default_case << ']';
}

std::ostream& operator<<(std::ostream& os, ObjectIsOp::Kind kind) {
  switch (kind) {
    case ObjectIsOp::Kind::kArrayBufferView:
      return os << "ArrayBufferView";
    case ObjectIsOp::Kind::kBigInt:
      return os << "BigInt";
    case ObjectIsOp::Kind::kBigInt64:
      return os << "BigInt64";
    case ObjectIsOp::Kind::kCallable:
      return os << "Callable";
    case ObjectIsOp::Kind::kConstructor:
      return os << "Constructor";
    case ObjectIsOp::Kind::kDetectableCallable:
      return os << "DetectableCallable";
    case ObjectIsOp::Kind::kInternalizedString:
      return os << "InternalizedString";
    case ObjectIsOp::Kind::kNonCallable:
      return os << "NonCallable";
    case ObjectIsOp::Kind::kNumber:
      return os << "Number";
    case ObjectIsOp::Kind::kNumberOrBigInt:
      return os << "NumberOrBigInt";
    case ObjectIsOp::Kind::kReceiver:
      return os << "Receiver";
    case ObjectIsOp::Kind::kReceiverOrNullOrUndefined:
      return os << "ReceiverOrNullOrUndefined";
    case ObjectIsOp::Kind::kSmi:
      return os << "Smi";
    case ObjectIsOp::Kind::kString:
      return os << "String";
    case ObjectIsOp::Kind::kStringOrStringWrapper:
      return os << "StringOrStringWrapper";
    case ObjectIsOp::Kind::kSymbol:
      return os << "Symbol";
    case ObjectIsOp::Kind::kUndetectable:
      return os << "Undetectable";
  }
}

std::ostream& operator<<(std::ostream& os,
                         ObjectIsOp::InputAssumptions input_assumptions) {
  switch (input_assumptions) {
    case ObjectIsOp::InputAssumptions::kNone:
      return os << "None";
    case ObjectIsOp::InputAssumptions::kHeapObject:
      return os << "HeapObject";
    case ObjectIsOp::InputAssumptions::kBigInt:
      return os << "BigInt";
  }
}

std::ostream& operator<<(std::ostream& os, NumericKind kind) {
  switch (kind) {
    case NumericKind::kFloat64Hole:
      return os << "Float64Hole";
    case NumericKind::kFinite:
      return os << "Finite";
    case NumericKind::kInteger:
      return os << "Integer";
    case NumericKind::kSafeInteger:
      return os << "SafeInteger";
    case NumericKind::kSmi:
      return os << "kSmi";
    case NumericKind::kMinusZero:
      return os << "MinusZero";
    case NumericKind::kNaN:
      return os << "NaN";
  }
}

std::ostream& operator<<(std::ostream& os, ConvertOp::Kind kind) {
  switch (kind) {
    case ConvertOp::Kind::kObject:
      return os << "Object";
    case ConvertOp::Kind::kBoolean:
      return os << "Boolean";
    case ConvertOp::Kind::kNumber:
      return os << "Number";
    case ConvertOp::Kind::kNumberOrOddball:
      return os << "NumberOrOddball";
    case ConvertOp::Kind::kPlainPrimitive:
      return os << "PlainPrimitive";
    case ConvertOp::Kind::kString:
      return os << "String";
    case ConvertOp::Kind::kSmi:
      return os << "Smi";
  }
}

std::ostream& operator<<(std::ostream& os,
                         ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind kind) {
  switch (kind) {
    case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kBigInt:
      return os << "BigInt";
    case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kBoolean:
      return os << "Boolean";
    case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kHeapNumber:
      return os << "HeapNumber";
    case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::
        kHeapNumberOrUndefined:
      return os << "HeapNumberOrUndefined";
    case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kNumber:
      return os << "Number";
    case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kSmi:
      return os << "Smi";
    case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kString:
      return os << "String";
  }
}

std::ostream& operator<<(
    std::ostream& os,
    ConvertUntaggedToJSPrimitiveOp::InputInterpretation input_interpretation) {
  switch (input_interpretation) {
    case ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned:
      return os << "Signed";
    case ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kUnsigned:
      return os << "Unsigned";
    case ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kCharCode:
      return os << "CharCode";
    case ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kCodePoint:
      return os << "CodePoint";
  }
}

std::ostream& operator<<(
    std::ostream& os,
    ConvertUntaggedToJSPrimitiveOrDeoptOp::JSPrimitiveKind kind) {
  switch (kind) {
    case ConvertUntaggedToJSPrimitiveOrDeoptOp::JSPrimitiveKind::kSmi:
      return os << "Smi";
  }
}

std::ostream& operator<<(
    std::ostream& os, ConvertUntaggedToJSPrimitiveOrDeoptOp::InputInterpretation
                          input_interpretation) {
  switch (input_interpretation) {
    case ConvertUntaggedToJSPrimitiveOrDeoptOp::InputInterpretation::kSigned:
      return os << "Signed";
    case ConvertUntaggedToJSPrimitiveOrDeoptOp::InputInterpretation::kUnsigned:
      return os << "Unsigned";
  }
}

std::ostream& operator<<(std::ostream& os,
                         ConvertJSPrimitiveToUntaggedOp::UntaggedKind kind) {
  switch (kind) {
    case ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kInt32:
      return os << "Int32";
    case ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kInt64:
      return os << "Int64";
    case ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kUint32:
      return os << "Uint32";
    case ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kBit:
      return os << "Bit";
    case ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kFloat64:
      return os << "Float64";
  }
}

std::ostream& operator<<(
    std::ostream& os,
    ConvertJSPrimitiveToUntaggedOp::InputAssumptions input_assumptions) {
  switch (input_assumptions) {
    case ConvertJSPrimitiveToUntaggedOp::InputAssumptions::kBoolean:
      return os << "Boolean";
    case ConvertJSPrimitiveToUntaggedOp::InputAssumptions::kSmi:
      return os << "Smi";
    case ConvertJSPrimitiveToUntaggedOp::InputAssumptions::kNumberOrOddball:
      return os << "NumberOrOddball";
    case ConvertJSPrimitiveToUntaggedOp::InputAssumptions::kPlainPrimitive:
      return os << "PlainPrimitive";
  }
}

std::ostream& operator<<(
    std::ostream& os,
    ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind kind) {
  switch (kind) {
    case ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt32:
      return os << "Int32";
    case ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt64:
      return os << "Int64";
    case ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kFloat64:
      return os << "Float64";
    case ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kArrayIndex:
      return os << "ArrayIndex";
  }
}

std::ostream& operator<<(
    std::ostream& os,
    ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind kind) {
  switch (kind) {
    case ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kNumber:
      return os << "Number";
    case ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::
        kNumberOrBoolean:
      return os << "NumberOrBoolean";
    case ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::
        kNumberOrOddball:
      return os << "NumberOrOddball";
    case ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::
        kNumberOrString:
      return os << "NumberOrString";
    case ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kSmi:
      return os << "Smi";
  }
}

std::ostream& operator<<(std::ostream& os,
                         TruncateJSPrimitiveToUntaggedOp::UntaggedKind kind) {
  switch (kind) {
    case TruncateJSPrimitiveToUntaggedOp::UntaggedKind::kInt32:
      return os << "Int32";
    case TruncateJSPrimitiveToUntaggedOp::UntaggedKind::kInt64:
      return os << "Int64";
    case TruncateJSPrimitiveToUntaggedOp::UntaggedKind::kBit:
      return os << "Bit";
  }
}

std::ostream& operator<<(
    std::ostream& os,
    TruncateJSPrimitiveToUntaggedOp::InputAssumptions input_assumptions) {
  switch (input_assumptions) {
    case TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kBigInt:
      return os << "BigInt";
    case TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kNumberOrOddball:
      return os << "NumberOrOddball";
    case TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kHeapObject:
      return os << "HeapObject";
    case TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kObject:
      return os << "Object";
  }
}

std::ostream& operator<<(
    std::ostream& os,
    TruncateJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind kind) {
  switch (kind) {
    case TruncateJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt32:
      return os << "Int32";
  }
}

std::ostream& operator<<(std::ostream& os, NewArrayOp::Kind kind) {
  switch (kind) {
    case NewArrayOp::Kind::kDouble:
      return os << "Double";
    case NewArrayOp::Kind::kObject:
      return os << "Object";
  }
}

std::ostream& operator<<(std::ostream& os, DoubleArrayMinMaxOp::Kind kind) {
  switch (kind) {
    case DoubleArrayMinMaxOp::Kind::kMin:
      return os << "Min";
    case DoubleArrayMinMaxOp::Kind::kMax:
      return os << "Max";
  }
}

std::ostream& operator<<(std::ostream& os, BigIntBinopOp::Kind kind) {
  switch (kind) {
    case BigIntBinopOp::Kind::kAdd:
      return os << "Add";
    case BigIntBinopOp::Kind::kSub:
      return os << "Sub";
    case BigIntBinopOp::Kind::kMul:
      return os << "Mul";
    case BigIntBinopOp::Kind::kDiv:
      return os << "Div";
    case BigIntBinopOp::Kind::kMod:
      return os << "Mod";
    case BigIntBinopOp::Kind::kBitwiseAnd:
      return os << "BitwiseAnd";
    case BigIntBinopOp::Kind::kBitwiseOr:
      return os << "BitwiseOr";
    case BigIntBinopOp::Kind::kBitwiseXor:
      return os << "BitwiseXor";
    case BigIntBinopOp::Kind::kShiftLeft:
      return os << "ShiftLeft";
    case BigIntBinopOp::Kind::kShiftRightArithmetic:
      return os << "ShiftRightArithmetic";
  }
}

std::ostream& operator<<(std::ostream& os, BigIntComparisonOp::Kind kind) {
  switch (kind) {
    case BigIntComparisonOp::Kind::kEqual:
      return os << "Equal";
    case BigIntComparisonOp::Kind::kLessThan:
      return os << "LessThan";
    case BigIntComparisonOp::Kind::kLessThanOrEqual:
      return os << "LessThanOrEqual";
  }
}

std::ostream& operator<<(std::ostream& os, BigIntUnaryOp::Kind kind) {
  switch (kind) {
    case BigIntUnaryOp::Kind::kNegate:
      return os << "Negate";
  }
}

std::ostream& operator<<(std::ostream& os, StringAtOp::Kind kind) {
  switch (kind) {
    case StringAtOp::Kind::kCharCode:
      return os << "CharCode";
    case StringAtOp::Kind::kCodePoint:
      return os << "CodePoint";
  }
}

#ifdef V8_INTL_SUPPORT
std::ostream& operator<<(std::ostream& os, StringToCaseIntlOp::Kind kind) {
  switch (kind) {
    case StringToCaseIntlOp::Kind::kLower:
      return os << "Lower";
    case StringToCaseIntlOp::Kind::kUpper:
      return os << "Upper";
  }
}
#endif  // V8_INTL_SUPPORT

std::ostream& operator<<(std::ostream& os, StringComparisonOp::Kind kind) {
  switch (kind) {
    case StringComparisonOp::Kind::kEqual:
      return os << "Equal";
    case StringComparisonOp::Kind::kLessThan:
      return os << "LessThan";
    case StringComparisonOp::Kind::kLessThanOrEqual:
      return os << "LessThanOrEqual";
  }
}

std::ostream& operator<<(std::ostream& os, ArgumentsLengthOp::Kind kind) {
  switch (kind) {
    case ArgumentsLengthOp::Kind::kArguments:
      return os << "Arguments";
    case ArgumentsLengthOp::Kind::kRest:
      return os << "Rest";
  }
}

std::ostream& operator<<(std::ostream& os,
                         TransitionAndStoreArrayElementOp::Kind kind) {
  switch (kind) {
    case TransitionAndStoreArrayElementOp::Kind::kElement:
      return os << "Element";
    case TransitionAndStoreArrayElementOp::Kind::kNumberElement:
      return os << "NumberElement";
    case TransitionAndStoreArrayElementOp::Kind::kOddballElement:
      return os << "OddballElement";
    case TransitionAndStoreArrayElementOp::Kind::kNonNumberElement:
      return os << "NonNumberElement";
    case TransitionAndStoreArrayElementOp::Kind::kSignedSmallElement:
      return os << "SignedSmallElement";
  }
}

std::ostream& operator<<(std::ostream& os, SameValueOp::Mode mode) {
  switch (mode) {
    case SameValueOp::Mode::kSameValue:
      return os << "SameValue";
    case SameValueOp::Mode::kSameValueNumbersOnly:
      return os << "SameValueNumbersOnly";
  }
}

std::ostream& operator<<(std::ostream& os, FindOrderedHashEntryOp::Kind kind) {
  switch (kind) {
    case FindOrderedHashEntryOp::Kind::kFindOrderedHashMapEntry:
      return os << "FindOrderedHashMapEntry";
    case FindOrderedHashEntryOp::Kind::kFindOrderedHashMapEntryForInt32Key:
      return os << "FindOrderedHashMapEntryForInt32Key";
    case FindOrderedHashEntryOp::Kind::kFindOrderedHashSetEntry:
      return os << "FindOrderedHashSetEntry";
  }
}

std::ostream& operator<<(std::ostream& os,
                         SpeculativeNumberBinopOp::Kind kind) {
  switch (kind) {
    case SpeculativeNumberBinopOp::Kind::kSafeIntegerAdd:
      return os << "SafeIntegerAdd";
  }
}

std::ostream& operator<<(std::ostream& os, JSStackCheckOp::Kind kind) {
  switch (kind) {
    case JSStackCheckOp::Kind::kFunctionEntry:
      return os << "function-entry";
    case JSStackCheckOp::Kind::kBuiltinEntry:
      return os << "builtin-entry";
    case JSStackCheckOp::Kind::kLoop:
      return os << "loop";
  }
}

#if V8_ENABLE_WEBASSEMBLY

const RegisterRepresentation& RepresentationFor(wasm::ValueType type) {
  static const RegisterRepresentation kWord32 =
      RegisterRepresentation::Word32();
  static const RegisterRepresentation kWord64 =
      RegisterRepresentation::Word64();
  static const RegisterRepresentation kFloat32 =
      RegisterRepresentation::Float32();
  static const RegisterRepresentation kFloat64 =
      RegisterRepresentation::Float64();
  static const RegisterRepresentation kTagged =
      RegisterRepresentation::Tagged();
  static const RegisterRepresentation kSimd128 =
      RegisterRepresentation::Simd128();

  switch (type.kind()) {
    case wasm::kI8:
    case wasm::kI16:
    case wasm::kI32:
      return kWord32;
    case wasm::kI64:
      return kWord64;
    case wasm::kF16:
    case wasm::kF32:
      return kFloat32;
    case wasm::kF64:
      return kFloat64;
    case wasm::kRefNull:
    case wasm::kRef:
      return kTagged;
    case wasm::kS128:
      return kSimd128;
    case wasm::kVoid:
    case wasm::kRtt:
    case wasm::kBottom:
      UNREACHABLE();
  }
}

namespace {
template <size_t size>
void PrintSimdValue(std::ostream& os, const uint8_t (&value)[size]) {
  os << "0x" << std::hex << std::setfill('0');
#ifdef V8_TARGET_BIG_ENDIAN
  for (int i = 0; i < static_cast<int>(size); i++) {
#else
  for (int i = static_cast<int>(size) - 1; i >= 0; i--) {
#endif
    os << std::setw(2) << static_cast<int>(value[i]);
  }
  os << std::dec << std::setfill(' ');
}
}  // namespace

void Simd128ConstantOp::PrintOptions(std::ostream& os) const {
  PrintSimdValue(os, value);
}

std::ostream& operator<<(std::ostream& os, Simd128BinopOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(kind)              \
  case Simd128BinopOp::Kind::k##kind: \
    return os << #kind;
    FOREACH_SIMD_128_BINARY_OPCODE(PRINT_KIND)
  }
#undef PRINT_KIND
}

std::ostream& operator<<(std::ostream& os, Simd128UnaryOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(kind)              \
  case Simd128UnaryOp::Kind::k##kind: \
    return os << #kind;
    FOREACH_SIMD_128_UNARY_OPCODE(PRINT_KIND)
  }
#undef PRINT_KIND
}

std::ostream& operator<<(std::ostream& os, Simd128ReduceOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(kind)               \
  case Simd128ReduceOp::Kind::k##kind: \
    return os << #kind;
    FOREACH_SIMD_128_REDUCE_OPTIONAL_OPCODE(PRINT_KIND)
  }
#undef PRINT_KIND
}

std::ostream& operator<<(std::ostream& os, Simd128ShiftOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(kind)              \
  case Simd128ShiftOp::Kind::k##kind: \
    return os << #kind;
    FOREACH_SIMD_128_SHIFT_OPCODE(PRINT_KIND)
  }
#undef PRINT_KIND
}

std::ostream& operator<<(std::ostream& os, Simd128TestOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(kind)             \
  case Simd128TestOp::Kind::k##kind: \
    return os << #kind;
    FOREACH_SIMD_128_TEST_OPCODE(PRINT_KIND)
  }
#undef PRINT_KIND
}

std::ostream& operator<<(std::ostream& os, Simd128SplatOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(kind)              \
  case Simd128SplatOp::Kind::k##kind: \
    return os << #kind;
    FOREACH_SIMD_128_SPLAT_OPCODE(PRINT_KIND)
  }
#undef PRINT_KIND
}

std::ostream& operator<<(std::ostream& os, Simd128TernaryOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(kind)                \
  case Simd128TernaryOp::Kind::k##kind: \
    return os << #kind;
    FOREACH_SIMD_128_TERNARY_OPCODE(PRINT_KIND)
  }
#undef PRINT_KIND
}

void Simd128ExtractLaneOp::PrintOptions(std::ostream& os) const {
  os << '[';
  switch (kind) {
    case Kind::kI8x16S:
      os << "I8x16S";
      break;
    case Kind::kI8x16U:
      os << "I8x16U";
      break;
    case Kind::kI16x8S:
      os << "I16x8S";
      break;
    case Kind::kI16x8U:
      os << "I16x8U";
      break;
    case Kind::kI32x4:
      os << "I32x4";
      break;
    case Kind::kI64x2:
      os << "I64x2";
      break;
    case Kind::kF16x8:
      os << "F16x8";
      break;
    case Kind::kF32x4:
      os << "F32x4";
      break;
    case Kind::kF64x2:
      os << "F64x2";
      break;
  }
  os << ", " << static_cast<int32_t>(lane) << ']';
}

void Simd128ReplaceLaneOp::PrintOptions(std::ostream& os) const {
  os << '[';
  switch (kind) {
    case Kind::kI8x16:
      os << "I8x16";
      break;
    case Kind::kI16x8:
      os << "I16x8";
      break;
    case Kind::kI32x4:
      os << "I32x4";
      break;
    case Kind::kI64x2:
      os << "I64x2";
      break;
    case Kind::kF16x8:
      os << "F16x8";
      break;
    case Kind::kF32x4:
      os << "F32x4";
      break;
    case Kind::kF64x2:
      os << "F64x2";
      break;
  }
  os << ", " << static_cast<int32_t>(lane) << ']';
}

void Simd128LaneMemoryOp::PrintOptions(std::ostream& os) const {
  os << '[' << (mode == Mode::kLoad ? "Load" : "Store") << ", ";
  if (kind.maybe_unaligned) os << "unaligned, ";
  if (kind.with_trap_handler) os << "protected, ";
  switch (lane_kind) {
    case LaneKind::k8:
      os << '8';
      break;
    case LaneKind::k16:
      os << "16";
      break;
    case LaneKind::k32:
      os << "32";
      break;
    case LaneKind::k64:
      os << "64";
      break;
  }
  os << "bit, lane: " << static_cast<int>(lane);
  if (offset != 0) os << ", offset: " << offset;
  os << ']';
}

void Simd128LoadTransformOp::PrintOptions(std::ostream& os) const {
  os << '[';
  if (load_kind.maybe_unaligned) os << "unaligned, ";
  if (load_kind.with_trap_handler) os << "protected, ";

  switch (transform_kind) {
#define PRINT_KIND(kind)       \
  case TransformKind::k##kind: \
    os << #kind;               \
    break;
    FOREACH_SIMD_128_LOAD_TRANSFORM_OPCODE(PRINT_KIND)
#undef PRINT_KIND
  }

  os << ", offset: " << offset << ']';
}

void Simd128ShuffleOp::PrintOptions(std::ostream& os) const {
  PrintSimdValue(os, shuffle);
}

#if V8_ENABLE_WASM_SIMD256_REVEC
void Simd256ConstantOp::PrintOptions(std::ostream& os) const {
  PrintSimdValue(os, value);
}

void Simd256Extract128LaneOp::PrintOptions(std::ostream& os) const {
  os << '[' << static_cast<int>(lane) << ']';
}

void Simd256LoadTransformOp::PrintOptions(std::ostream& os) const {
  os << '[';
  if (load_kind.maybe_unaligned) os << "unaligned, ";
  if (load_kind.with_trap_handler) os << "protected, ";

  switch (transform_kind) {
#define PRINT_KIND(kind)       \
  case TransformKind::k##kind: \
    os << #kind;               \
    break;
    FOREACH_SIMD_256_LOAD_TRANSFORM_OPCODE(PRINT_KIND)
#undef PRINT_KIND
  }

  os << ", offset: " << offset << ']';
}

std::ostream& operator<<(std::ostream& os, Simd256UnaryOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(kind)              \
  case Simd256UnaryOp::Kind::k##kind: \
    return os << #kind;
    FOREACH_SIMD_256_UNARY_OPCODE(PRINT_KIND)
  }
#undef PRINT_KIND
}

std::ostream& operator<<(std::ostream& os, Simd256TernaryOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(kind)                \
  case Simd256TernaryOp::Kind::k##kind: \
    return os << #kind;
    FOREACH_SIMD_256_TERNARY_OPCODE(PRINT_KIND)
  }
#undef PRINT_KIND
}

std::ostream& operator<<(std::ostream& os, Simd256BinopOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(kind)              \
  case Simd256BinopOp::Kind::k##kind: \
    return os << #kind;
    FOREACH_SIMD_256_BINARY_OPCODE(PRINT_KIND)
  }
#undef PRINT_KIND
}

std::ostream& operator<<(std::ostream& os, Simd256ShiftOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(kind)              \
  case Simd256ShiftOp::Kind::k##kind: \
    return os << #kind;
    FOREACH_SIMD_256_SHIFT_OPCODE(PRINT_KIND)
  }
#undef PRINT_KIND
}

std::ostream& operator<<(std::ostream& os, Simd256SplatOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(kind)              \
  case Simd256SplatOp::Kind::k##kind: \
    return os << #kind;
    FOREACH_SIMD_256_SPLAT_OPCODE(PRINT_KIND)
  }
#undef PRINT_KIND
}

#ifdef V8_TARGET_ARCH_X64
void Simd256ShufdOp::PrintOptions(std::ostream& os) const {
  os << '[' << std::bitset<8>(control) << ']';
}

void Simd256ShufpsOp::PrintOptions(std::ostream& os) const {
  os << '[' << std::bitset<8>(control) << ']';
}

std::ostream& operator<<(std::ostream& os, Simd256UnpackOp::Kind kind) {
  switch (kind) {
#define PRINT_KIND(kind)               \
  case Simd256UnpackOp::Kind::k##kind: \
    return os << #kind;
    FOREACH_SIMD_256_UNPACK_OPCODE(PRINT_KIND)
  }
#undef PRINT_KIND
}
#endif  // V8_TARGET_ARCH_X64
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

void WasmAllocateArrayOp::PrintOptions(std::ostream& os) const {
  os << '[' << array_type->element_type() << ']';
}

void ArrayGetOp::PrintOptions(std::ostream& os) const {
  os << '[' << (is_signed ? "signed " : "")
     << (array_type->mutability() ? "" : "immutable ")
     << array_type->element_type() << ']';
}

#endif  // V8_ENABLE_WEBASSEBMLY

std::string Operation::ToString() const {
  std::stringstream ss;
  ss << *this;
  return ss.str();
}

base::LazyMutex SupportedOperations::mutex_ = LAZY_MUTEX_INITIALIZER;
SupportedOperations SupportedOperations::instance_;
bool SupportedOperations::initialized_;

void SupportedOperations::Initialize() {
  base::MutexGuard lock(mutex_.Pointer());
  if (initialized_) return;
  initialized_ = true;

  MachineOperatorBuilder::Flags supported =
      InstructionSelector::SupportedMachineOperatorFlags();
#define SET_SUPPORTED(name, machine_name) \
  instance_.name##_ = supported & MachineOperatorBuilder::Flag::k##machine_name;

  SUPPORTED_OPERATIONS_LIST(SET_SUPPORTED)
#undef SET_SUPPORTED
}

base::SmallVector<Block*, 4> SuccessorBlocks(const Block& block,
                                             const Graph& graph) {
  return SuccessorBlocks(block.LastOperation(graph));
}

// static
bool SupportedOperations::IsUnalignedLoadSupported(MemoryRepresentation repr) {
  return InstructionSelector::AlignmentRequirements().IsUnalignedLoadSupported(
      repr.ToMachineType().representation());
}

// static
bool SupportedOperations::IsUnalignedStoreSupported(MemoryRepresentation repr) {
  return InstructionSelector::AlignmentRequirements().IsUnalignedStoreSupported(
      repr.ToMachineType().representation());
}

void CheckExceptionOp::Validate(const Graph& graph) const {
  DCHECK_NE(didnt_throw_block, catch_block);
  // `CheckException` should follow right after the throwing operation.
  DCHECK_EQ(throwing_operation(),
            V<Any>::Cast(graph.PreviousIndex(graph.Index(*this))));
}

namespace {
BlockIndex index_for_bound_block(const Block* block) {
  DCHECK_NOT_NULL(block);
  const BlockIndex index = block->index();
  DCHECK(index.valid());
  return index;
}
}  // namespace

size_t CallOp::hash_value(HashingStrategy strategy) const {
  if (strategy == HashingStrategy::kMakeSnapshotStable) {
    // Destructure here to cause a compilation error in case `options` is
    // changed.
    auto [descriptor_value, callee_effects] = options();
    return HashWithOptions(*descriptor_value, callee_effects);
  } else {
    return Base::hash_value(strategy);
  }
}

size_t CheckExceptionOp::hash_value(HashingStrategy strategy) const {
  if (strategy == HashingStrategy::kMakeSnapshotStable) {
    // Destructure here to cause a compilation error in case `options` is
    // changed.
    auto [didnt_throw_block_value, catch_block_value] = options();
    return HashWithOptions(index_for_bound_block(didnt_throw_block_value),
                           index_for_bound_block(catch_block_value));
  } else {
    return Base::hash_value(strategy);
  }
}

size_t GotoOp::hash_value(HashingStrategy strategy) const {
  if (strategy == HashingStrategy::kMakeSnapshotStable) {
    // Destructure here to cause a compilation error in case `options` is
    // changed.
    auto [destination_value, is_backedge_value] = options();
    return HashWithOptions(index_for_bound_block(destination_value),
                           is_backedge_value);
  } else {
    return Base::hash_value(strategy);
  }
}

size_t BranchOp::hash_value(HashingStrategy strategy) const {
  if (strategy == HashingStrategy::kMakeSnapshotStable) {
    // Destructure here to cause a compilation error in case `options` is
    // changed.
    auto [if_true_value, if_false_value, hint_value] = options();
    return HashWithOptions(index_for_bound_block(if_true_value),
                           index_for_bound_block(if_false_value), hint_value);
  } else {
    return Base::hash_value(strategy);
  }
}

size_t SwitchOp::hash_value(HashingStrategy strategy) const {
  if (strategy == HashingStrategy::kMakeSnapshotStable) {
    // Destructure here to cause a compilation error in case `options` is
    // changed.
    auto [cases_value, default_case_value, default_hint_value] = options();
    DCHECK_NOT_NULL(default_case_value);
    size_t hash = HashWithOptions(index_for_bound_block(default_case_value),
                                  default_hint_value);
    for (const auto& c : cases_value) {
      hash = fast_hash_combine(hash, c.value,
                               index_for_bound_block(c.destination), c.hint);
    }
    return hash;
  } else {
    return Base::hash_value(strategy);
  }
}

namespace {
// Ensures basic consistency of representation mapping.
class InputsRepFactoryCheck : InputsRepFactory {
  static_assert(*ToMaybeRepPointer(RegisterRepresentation::Word32()) ==
                MaybeRegisterRepresentation::Word32());
  static_assert(*ToMaybeRepPointer(RegisterRepresentation::Word64()) ==
                MaybeRegisterRepresentation::Word64());
  static_assert(*ToMaybeRepPointer(RegisterRepresentation::Float32()) ==
                MaybeRegisterRepresentation::Float32());
  static_assert(*ToMaybeRepPointer(RegisterRepresentation::Float64()) ==
                MaybeRegisterRepresentation::Float64());
  static_assert(*ToMaybeRepPointer(RegisterRepresentation::Tagged()) ==
                MaybeRegisterRepresentation::Tagged());
  static_assert(*ToMaybeRepPointer(RegisterRepresentation::Compressed()) ==
                MaybeRegisterRepresentation::Compressed());
  static_assert(*ToMaybeRepPointer(RegisterRepresentation::Simd128()) ==
                MaybeRegisterRepresentation::Simd128());
};
}  // namespace

bool IsUnlikelySuccessor(const Block* block, const Block* successor,
                         const Graph& graph) {
  DCHECK(base::contains(successor->Predecessors(), block));
  const Operation& terminator = block->LastOperation(graph);
  switch (terminator.opcode) {
    case Opcode::kCheckException: {
      const CheckExceptionOp& check_exception =
          terminator.Cast<CheckExceptionOp>();
      return successor == check_exception.catch_block;
    }
    case Opcode::kGoto:
      return false;
    case Opcode::kBranch: {
      const BranchOp& branch = terminator.Cast<BranchOp>();
      return (branch.hint == BranchHint::kTrue &&
              successor == branch.if_false) ||
             (branch.hint == BranchHint::kFalse && successor == branch.if_true);
    }
    case Opcode::kSwitch: {
      const SwitchOp& swtch = terminator.Cast<SwitchOp>();
      if (successor == swtch.default_case) {
        return swtch.default_hint == BranchHint::kFalse;
      }
      auto it = std::find_if(swtch.cases.begin(), swtch.cases.end(),
                             [successor](const SwitchOp::Case& c) {
                               return c.destination == successor;
                             });
      DCHECK_NE(it, swtch.cases.end());
      return it->hint == BranchHint::kFalse;
    }
    case Opcode::kDeoptimize:
    case Opcode::kTailCall:
    case Opcode::kUnreachable:
    case Opcode::kReturn:
      UNREACHABLE();

#define NON_TERMINATOR_CASE(op) case Opcode::k##op:
      TURBOSHAFT_OPERATION_LIST_NOT_BLOCK_TERMINATOR(NON_TERMINATOR_CASE)
      UNREACHABLE();
#undef NON_TERMINATOR_CASE
  }
}

bool Operation::IsOnlyUserOf(const Operation& value, const Graph& graph) const {
  DCHECK_GE(std::count(inputs().begin(), inputs().end(), graph.Index(value)),
            1);
  if (value.saturated_use_count.IsOne()) return true;
  return std::count(inputs().begin(), inputs().end(), graph.Index(value)) ==
         value.saturated_use_count.Get();
}

}  // namespace v8::internal::compiler::turboshaft
