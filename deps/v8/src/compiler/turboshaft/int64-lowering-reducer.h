// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_TURBOSHAFT_INT64_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_INT64_LOWERING_REDUCER_H_

#include "src/codegen/machine-type.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/wasm-compiler.h"
#include "src/compiler/wasm-graph-assembler.h"
#include "src/wasm/wasm-engine.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// This reducer is run on 32 bit platforms to lower unsupported 64 bit integer
// operations to supported 32 bit operations.
template <class Next>
class Int64LoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(Int64Lowering)

  Int64LoweringReducer() {
    wasm::CallOrigin origin = __ data() -> is_js_to_wasm()
                                  ? wasm::kCalledFromJS
                                  : wasm::kCalledFromWasm;
    sig_ = CreateMachineSignature(zone_, __ data()->wasm_sig(), origin);

    InitializeIndexMaps();
  }

  V<Word> REDUCE(WordBinop)(V<Word> left, V<Word> right, WordBinopOp::Kind kind,
                            WordRepresentation rep) {
    if (rep == WordRepresentation::Word64()) {
      V<Word64> left_w64 = V<Word64>::Cast(left);
      V<Word64> right_w64 = V<Word64>::Cast(right);
      switch (kind) {
        case WordBinopOp::Kind::kAdd:
          return LowerPairBinOp(left_w64, right_w64,
                                Word32PairBinopOp::Kind::kAdd);
        case WordBinopOp::Kind::kSub:
          return LowerPairBinOp(left_w64, right_w64,
                                Word32PairBinopOp::Kind::kSub);
        case WordBinopOp::Kind::kMul:
          return LowerPairBinOp(left_w64, right_w64,
                                Word32PairBinopOp::Kind::kMul);
        case WordBinopOp::Kind::kBitwiseAnd:
          return LowerBitwiseAnd(left_w64, right_w64);
        case WordBinopOp::Kind::kBitwiseOr:
          return LowerBitwiseOr(left_w64, right_w64);
        case WordBinopOp::Kind::kBitwiseXor:
          return LowerBitwiseXor(left_w64, right_w64);
        default:
          FATAL("WordBinopOp kind %d not supported by int64 lowering",
                static_cast<int>(kind));
      }
    }
    return Next::ReduceWordBinop(left, right, kind, rep);
  }

  OpIndex REDUCE(Shift)(OpIndex left, OpIndex right, ShiftOp::Kind kind,
                        WordRepresentation rep) {
    if (rep == WordRepresentation::Word64()) {
      switch (kind) {
        case ShiftOp::Kind::kShiftLeft:
          return LowerPairShiftOp(left, right,
                                  Word32PairBinopOp::Kind::kShiftLeft);
        case ShiftOp::Kind::kShiftRightArithmetic:
          return LowerPairShiftOp(
              left, right, Word32PairBinopOp::Kind::kShiftRightArithmetic);
        case ShiftOp::Kind::kShiftRightLogical:
          return LowerPairShiftOp(left, right,
                                  Word32PairBinopOp::Kind::kShiftRightLogical);
        case ShiftOp::Kind::kRotateRight:
          return LowerRotateRight(left, right);
        default:
          FATAL("Shiftop kind %d not supported by int64 lowering",
                static_cast<int>(kind));
      }
    }
    return Next::ReduceShift(left, right, kind, rep);
  }

  V<Word32> REDUCE(Comparison)(V<Any> left, V<Any> right,
                               ComparisonOp::Kind kind,
                               RegisterRepresentation rep) {
    if (rep != WordRepresentation::Word64()) {
      return Next::ReduceComparison(left, right, kind, rep);
    }

    auto [left_low, left_high] = Unpack(V<Word64>::Cast(left));
    auto [right_low, right_high] = Unpack(V<Word64>::Cast(right));
    V<Word32> high_comparison;
    V<Word32> low_comparison;
    switch (kind) {
      case ComparisonOp::Kind::kEqual:
        // TODO(wasm): Use explicit comparisons and && here?
        return __ Word32Equal(
            __ Word32BitwiseOr(__ Word32BitwiseXor(left_low, right_low),
                               __ Word32BitwiseXor(left_high, right_high)),
            0);
      case ComparisonOp::Kind::kSignedLessThan:
        high_comparison = __ Int32LessThan(left_high, right_high);
        low_comparison = __ Uint32LessThan(left_low, right_low);
        break;
      case ComparisonOp::Kind::kSignedLessThanOrEqual:
        high_comparison = __ Int32LessThan(left_high, right_high);
        low_comparison = __ Uint32LessThanOrEqual(left_low, right_low);
        break;
      case ComparisonOp::Kind::kUnsignedLessThan:
        high_comparison = __ Uint32LessThan(left_high, right_high);
        low_comparison = __ Uint32LessThan(left_low, right_low);
        break;
      case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
        high_comparison = __ Uint32LessThan(left_high, right_high);
        low_comparison = __ Uint32LessThanOrEqual(left_low, right_low);
        break;
    }

    return __ Word32BitwiseOr(
        high_comparison,
        __ Word32BitwiseAnd(__ Word32Equal(left_high, right_high),
                            low_comparison));
  }

  V<Any> REDUCE(Call)(V<CallTarget> callee, OptionalV<FrameState> frame_state,
                      base::Vector<const OpIndex> arguments,
                      const TSCallDescriptor* descriptor, OpEffects effects) {
    const bool is_tail_call = false;
    return LowerCall(callee, frame_state, arguments, descriptor, effects,
                     is_tail_call);
  }

  OpIndex REDUCE(TailCall)(OpIndex callee,
                           base::Vector<const OpIndex> arguments,
                           const TSCallDescriptor* descriptor) {
    const bool is_tail_call = true;
    OpIndex frame_state = OpIndex::Invalid();
    return LowerCall(callee, frame_state, arguments, descriptor,
                     OpEffects().CanCallAnything(), is_tail_call);
  }

  OpIndex REDUCE(Constant)(ConstantOp::Kind kind, ConstantOp::Storage value) {
    if (kind == ConstantOp::Kind::kWord64) {
      uint32_t high = value.integral >> 32;
      uint32_t low = value.integral & std::numeric_limits<uint32_t>::max();
      return __ Tuple(__ Word32Constant(low), __ Word32Constant(high));
    }
    return Next::ReduceConstant(kind, value);
  }

  OpIndex REDUCE(Parameter)(int32_t parameter_index, RegisterRepresentation rep,
                            const char* debug_name = "") {
    int32_t param_count = static_cast<int32_t>(sig_->parameter_count());
    // Handle special indices (closure, context).
    if (parameter_index < 0) {
      return Next::ReduceParameter(parameter_index, rep, debug_name);
    }
    if (parameter_index > param_count) {
      DCHECK_NE(rep, RegisterRepresentation::Word64());
      int param_offset =
          std::count(sig_->parameters().begin(), sig_->parameters().end(),
                     MachineRepresentation::kWord64);
      return Next::ReduceParameter(parameter_index + param_offset, rep,
                                   debug_name);
    }
    int32_t new_index = param_index_map_[parameter_index];
    if (rep == RegisterRepresentation::Word64()) {
      rep = RegisterRepresentation::Word32();
      return __ Tuple(Next::ReduceParameter(new_index, rep),
                      Next::ReduceParameter(new_index + 1, rep));
    }
    return Next::ReduceParameter(new_index, rep, debug_name);
  }

  OpIndex REDUCE(Return)(OpIndex pop_count,
                         base::Vector<const OpIndex> return_values) {
    if (!returns_i64_) {
      return Next::ReduceReturn(pop_count, return_values);
    }
    base::SmallVector<OpIndex, 8> lowered_values;
    for (size_t i = 0; i < sig_->return_count(); ++i) {
      if (sig_->GetReturn(i) == MachineRepresentation::kWord64) {
        auto [low, high] = Unpack(return_values[i]);
        lowered_values.push_back(low);
        lowered_values.push_back(high);
      } else {
        lowered_values.push_back(return_values[i]);
      }
    }
    return Next::ReduceReturn(pop_count, base::VectorOf(lowered_values));
  }

  V<Word> REDUCE(WordUnary)(V<Word> input, WordUnaryOp::Kind kind,
                            WordRepresentation rep) {
    if (rep == RegisterRepresentation::Word64()) {
      V<Word64> input_w64 = V<Word64>::Cast(input);
      switch (kind) {
        case WordUnaryOp::Kind::kCountLeadingZeros:
          return LowerClz(input_w64);
        case WordUnaryOp::Kind::kCountTrailingZeros:
          return LowerCtz(input_w64);
        case WordUnaryOp::Kind::kPopCount:
          return LowerPopCount(input_w64);
        case WordUnaryOp::Kind::kSignExtend8:
          return LowerSignExtend(__ Word32SignExtend8(Unpack(input_w64).first));
        case WordUnaryOp::Kind::kSignExtend16:
          return LowerSignExtend(
              __ Word32SignExtend16(Unpack(input_w64).first));
        case WordUnaryOp::Kind::kReverseBytes: {
          auto [low, high] = Unpack(input_w64);
          V<Word32> reversed_low = __ Word32ReverseBytes(low);
          V<Word32> reversed_high = __ Word32ReverseBytes(high);
          return V<Word64>::Cast(__ Tuple(reversed_high, reversed_low));
        }
        default:
          FATAL("WordUnaryOp kind %d not supported by int64 lowering",
                static_cast<int>(kind));
      }
    }
    return Next::ReduceWordUnary(input, kind, rep);
  }

  OpIndex REDUCE(Change)(OpIndex input, ChangeOp::Kind kind,
                         ChangeOp::Assumption assumption,
                         RegisterRepresentation from,
                         RegisterRepresentation to) {
    auto word32 = RegisterRepresentation::Word32();
    auto word64 = RegisterRepresentation::Word64();
    auto float64 = RegisterRepresentation::Float64();
    using Kind = ChangeOp::Kind;
    if (from != word64 && to != word64) {
      return Next::ReduceChange(input, kind, assumption, from, to);
    }

    if (from == word32 && to == word64) {
      if (kind == Kind::kZeroExtend) {
        return __ Tuple(V<Word32>::Cast(input), __ Word32Constant(0));
      }
      if (kind == Kind::kSignExtend) {
        return LowerSignExtend(input);
      }
    }
    if (from == float64 && to == word64) {
      if (kind == Kind::kBitcast) {
        return __ Tuple(__ Float64ExtractLowWord32(input),
                        __ Float64ExtractHighWord32(input));
      }
    }
    if (from == word64 && to == float64) {
      if (kind == Kind::kBitcast) {
        auto input_w32p = V<Tuple<Word32, Word32>>::Cast(input);
        return __ BitcastWord32PairToFloat64(
            __ template Projection<1>(input_w32p),
            __ template Projection<0>(input_w32p));
      }
    }
    if (from == word64 && to == word32 && kind == Kind::kTruncate) {
      auto input_w32p = V<Tuple<Word32, Word32>>::Cast(input);
      return __ template Projection<0>(input_w32p);
    }
    std::stringstream str;
    str << "ChangeOp " << kind << " from " << from << " to " << to
        << "not supported by int64 lowering";
    FATAL("%s", str.str().c_str());
  }

  std::pair<OptionalV<Word32>, int32_t> IncreaseOffset(OptionalV<Word32> index,
                                                       int32_t offset,
                                                       int32_t add_offset,
                                                       bool tagged_base) {
    // Note that the offset will just wrap around. Still, we need to always
    // use an offset that is not std::numeric_limits<int32_t>::min() on tagged
    // loads.
    // TODO(dmercadier): Replace LoadOp::OffsetIsValid by taking care of this
    // special case in the LoadStoreSimplificationReducer instead.
    int32_t new_offset =
        static_cast<uint32_t>(offset) + static_cast<uint32_t>(add_offset);
    OptionalV<Word32> new_index = index;
    if (!LoadOp::OffsetIsValid(new_offset, tagged_base)) {
      // We cannot encode the new offset so we use the old offset
      // instead and use the Index to represent the extra offset.
      new_offset = offset;
      if (index.has_value()) {
        new_index = __ Word32Add(new_index.value(), add_offset);
      } else {
        new_index = __ Word32Constant(sizeof(int32_t));
      }
    }
    return {new_index, new_offset};
  }

  OpIndex REDUCE(Load)(OpIndex base, OptionalOpIndex index, LoadOp::Kind kind,
                       MemoryRepresentation loaded_rep,
                       RegisterRepresentation result_rep, int32_t offset,
                       uint8_t element_scale) {
    if (kind.is_atomic) {
      if (loaded_rep == MemoryRepresentation::Int64() ||
          loaded_rep == MemoryRepresentation::Uint64()) {
        // TODO(jkummerow): Support non-zero scales in AtomicWord32PairOp, and
        // remove the corresponding bailout in MachineOptimizationReducer to
        // allow generating them.
        CHECK_EQ(element_scale, 0);
        return __ AtomicWord32PairLoad(base, index, offset);
      }
      if (result_rep == RegisterRepresentation::Word64()) {
        return __ Tuple(
            __ Load(base, index, kind, loaded_rep,
                    RegisterRepresentation::Word32(), offset, element_scale),
            __ Word32Constant(0));
      }
    }
    if (loaded_rep == MemoryRepresentation::Int64() ||
        loaded_rep == MemoryRepresentation::Uint64()) {
      auto [high_index, high_offset] =
          IncreaseOffset(index, offset, sizeof(int32_t), kind.tagged_base);
      return __ Tuple(
          Next::ReduceLoad(base, index, kind, MemoryRepresentation::Int32(),
                           RegisterRepresentation::Word32(), offset,
                           element_scale),
          Next::ReduceLoad(
              base, high_index, kind, MemoryRepresentation::Int32(),
              RegisterRepresentation::Word32(), high_offset, element_scale));
    }
    return Next::ReduceLoad(base, index, kind, loaded_rep, result_rep, offset,
                            element_scale);
  }

  OpIndex REDUCE(Store)(OpIndex base, OptionalOpIndex index, OpIndex value,
                        StoreOp::Kind kind, MemoryRepresentation stored_rep,
                        WriteBarrierKind write_barrier, int32_t offset,
                        uint8_t element_size_log2,
                        bool maybe_initializing_or_transitioning,
                        IndirectPointerTag maybe_indirect_pointer_tag) {
    if (stored_rep == MemoryRepresentation::Int64() ||
        stored_rep == MemoryRepresentation::Uint64()) {
      auto [low, high] = Unpack(value);
      if (kind.is_atomic) {
        // TODO(jkummerow): Support non-zero scales in AtomicWord32PairOp, and
        // remove the corresponding bailout in MachineOptimizationReducer to
        // allow generating them.
        CHECK_EQ(element_size_log2, 0);
        return __ AtomicWord32PairStore(base, index, low, high, offset);
      }
      OpIndex low_store = Next::ReduceStore(
          base, index, low, kind, MemoryRepresentation::Int32(), write_barrier,
          offset, element_size_log2, maybe_initializing_or_transitioning,
          maybe_indirect_pointer_tag);
      auto [high_index, high_offset] =
          IncreaseOffset(index, offset, sizeof(int32_t), kind.tagged_base);
      OpIndex high_store = Next::ReduceStore(
          base, high_index, high, kind, MemoryRepresentation::Int32(),
          write_barrier, high_offset, element_size_log2,
          maybe_initializing_or_transitioning, maybe_indirect_pointer_tag);
      return __ Tuple(low_store, high_store);
    }
    return Next::ReduceStore(base, index, value, kind, stored_rep,
                             write_barrier, offset, element_size_log2,
                             maybe_initializing_or_transitioning,
                             maybe_indirect_pointer_tag);
  }

  OpIndex REDUCE(AtomicRMW)(OpIndex base, OpIndex index, OpIndex value,
                            OptionalOpIndex expected, AtomicRMWOp::BinOp bin_op,
                            RegisterRepresentation in_out_rep,
                            MemoryRepresentation memory_rep,
                            MemoryAccessKind kind) {
    if (in_out_rep != RegisterRepresentation::Word64()) {
      return Next::ReduceAtomicRMW(base, index, value, expected, bin_op,
                                   in_out_rep, memory_rep, kind);
    }
    auto [value_low, value_high] = Unpack(value);
    if (memory_rep == MemoryRepresentation::Int64() ||
        memory_rep == MemoryRepresentation::Uint64()) {
      if (bin_op == AtomicRMWOp::BinOp::kCompareExchange) {
        auto [expected_low, expected_high] = Unpack(expected.value());
        return __ AtomicWord32PairCompareExchange(
            base, index, value_low, value_high, expected_low, expected_high);
      } else {
        return __ AtomicWord32PairBinop(base, index, value_low, value_high,
                                        bin_op);
      }
    }

    OpIndex new_expected = OpIndex::Invalid();
    if (bin_op == AtomicRMWOp::BinOp::kCompareExchange) {
      auto [expected_low, expected_high] = Unpack(expected.value());
      new_expected = expected_low;
    }
    return __ Tuple(Next::ReduceAtomicRMW(
                        base, index, value_low, new_expected, bin_op,
                        RegisterRepresentation::Word32(), memory_rep, kind),
                    __ Word32Constant(0));
  }

  OpIndex REDUCE(Phi)(base::Vector<const OpIndex> inputs,
                      RegisterRepresentation rep) {
    if (rep == RegisterRepresentation::Word64()) {
      base::SmallVector<OpIndex, 8> inputs_low;
      base::SmallVector<OpIndex, 8> inputs_high;
      auto word32 = RegisterRepresentation::Word32();
      inputs_low.reserve(inputs.size());
      inputs_high.reserve(inputs.size());
      for (OpIndex input : inputs) {
        auto input_w32p = V<Tuple<Word32, Word32>>::Cast(input);
        inputs_low.push_back(__ template Projection<0>(input_w32p));
        inputs_high.push_back(__ template Projection<1>(input_w32p));
      }
      return __ Tuple(Next::ReducePhi(base::VectorOf(inputs_low), word32),
                      Next::ReducePhi(base::VectorOf(inputs_high), word32));
    }
    return Next::ReducePhi(inputs, rep);
  }

  OpIndex REDUCE(PendingLoopPhi)(OpIndex input, RegisterRepresentation rep) {
    if (rep == RegisterRepresentation::Word64()) {
      auto input_w32p = V<Tuple<Word32, Word32>>::Cast(input);
      V<Word32> low = __ PendingLoopPhi(__ template Projection<0>(input_w32p));
      V<Word32> high = __ PendingLoopPhi(__ template Projection<1>(input_w32p));
      return __ Tuple(low, high);
    }
    return Next::ReducePendingLoopPhi(input, rep);
  }

  void FixLoopPhi(const PhiOp& input_phi, OpIndex output_index,
                  Block* output_graph_loop) {
    if (input_phi.rep == RegisterRepresentation::Word64()) {
      const TupleOp& tuple = __ Get(output_index).template Cast<TupleOp>();
      DCHECK_EQ(tuple.input_count, 2);
      OpIndex new_inputs[2] = {__ MapToNewGraph(input_phi.input(0)),
                               __ MapToNewGraph(input_phi.input(1))};
      for (size_t i = 0; i < 2; ++i) {
        OpIndex phi_index = tuple.input(i);
        if (!output_graph_loop->Contains(phi_index)) {
          continue;
        }
#ifdef DEBUG
        const PendingLoopPhiOp& pending_phi =
            __ Get(phi_index).template Cast<PendingLoopPhiOp>();
        DCHECK_EQ(pending_phi.rep, RegisterRepresentation::Word32());
        DCHECK_EQ(
            pending_phi.first(),
            __ Projection(new_inputs[0], i, RegisterRepresentation::Word32()));
#endif
        __ output_graph().template Replace<PhiOp>(
            phi_index,
            base::VectorOf({__ Projection(new_inputs[0], i,
                                          RegisterRepresentation::Word32()),
                            __ Projection(new_inputs[1], i,
                                          RegisterRepresentation::Word32())}),
            RegisterRepresentation::Word32());
      }
      return;
    }
    return Next::FixLoopPhi(input_phi, output_index, output_graph_loop);
  }

  V<Simd128> REDUCE(Simd128Splat)(V<Any> input, Simd128SplatOp::Kind kind) {
    // TODO(14108): Introduce I32-pair splat for better codegen.
    if (kind != Simd128SplatOp::Kind::kI64x2) {
      return Next::ReduceSimd128Splat(input, kind);
    }
    auto [low, high] = Unpack(V<Word64>::Cast(input));
    V<Simd128> base = __ Simd128Splat(low, Simd128SplatOp::Kind::kI32x4);
    V<Simd128> first_replaced = __ Simd128ReplaceLane(
        base, high, Simd128ReplaceLaneOp::Kind::kI32x4, 1);
    return __ Simd128ReplaceLane(first_replaced, high,
                                 Simd128ReplaceLaneOp::Kind::kI32x4, 3);
  }

  V<Any> REDUCE(Simd128ExtractLane)(V<Simd128> input,
                                    Simd128ExtractLaneOp::Kind kind,
                                    uint8_t lane) {
    if (kind != Simd128ExtractLaneOp::Kind::kI64x2) {
      return Next::ReduceSimd128ExtractLane(input, kind, lane);
    }
    V<Word32> low = V<Word32>::Cast(__ Simd128ExtractLane(
        input, Simd128ExtractLaneOp::Kind::kI32x4, 2 * lane));
    V<Word32> high = V<Word32>::Cast(__ Simd128ExtractLane(
        input, Simd128ExtractLaneOp::Kind::kI32x4, 2 * lane + 1));
    return __ Tuple(low, high);
  }

  V<Simd128> REDUCE(Simd128ReplaceLane)(V<Simd128> into, V<Any> new_lane,
                                        Simd128ReplaceLaneOp::Kind kind,
                                        uint8_t lane) {
    // TODO(14108): Introduce I32-pair lane replacement for better codegen.
    if (kind != Simd128ReplaceLaneOp::Kind::kI64x2) {
      return Next::ReduceSimd128ReplaceLane(into, new_lane, kind, lane);
    }
    auto [low, high] = Unpack(V<Word64>::Cast(new_lane));
    V<Simd128> low_replaced = __ Simd128ReplaceLane(
        into, low, Simd128ReplaceLaneOp::Kind::kI32x4, 2 * lane);
    return __ Simd128ReplaceLane(
        low_replaced, high, Simd128ReplaceLaneOp::Kind::kI32x4, 2 * lane + 1);
  }

  V<turboshaft::FrameState> REDUCE(FrameState)(
      base::Vector<const OpIndex> inputs, bool inlined,
      const FrameStateData* data) {
    bool has_int64_input = false;

    for (MachineType type : data->machine_types) {
      if (RegisterRepresentation::FromMachineType(type) ==
          RegisterRepresentation::Word64()) {
        has_int64_input = true;
        break;
      }
    }
    if (!has_int64_input) {
      return Next::ReduceFrameState(inputs, inlined, data);
    }
    FrameStateData::Builder builder;
    if (inlined) {
      builder.AddParentFrameState(V<turboshaft::FrameState>(inputs[0]));
    }
    const FrameStateFunctionInfo* function_info =
        data->frame_state_info.function_info();
    uint16_t lowered_parameter_count = function_info->parameter_count();
    int lowered_local_count = function_info->local_count();

    for (size_t i = inlined; i < inputs.size(); ++i) {
      // In case of inlining the parent FrameState is an additional input,
      // however, it doesn't have an entry in the machine_types vector, so that
      // index has to be adapted.
      size_t machine_type_index = i - inlined;
      if (RegisterRepresentation::FromMachineType(
              data->machine_types[machine_type_index]) ==
          RegisterRepresentation::Word64()) {
        auto [low, high] = Unpack(V<Word64>::Cast(inputs[i]));
        builder.AddInput(MachineType::Int32(), low);
        builder.AddInput(MachineType::Int32(), high);
        if (i < inlined + function_info->parameter_count()) {
          ++lowered_parameter_count;
        } else {
          ++lowered_local_count;
        }
      } else {
        // Just copy over the existing input.
        builder.AddInput(data->machine_types[machine_type_index], inputs[i]);
      }
    }
    Zone* zone = Asm().data()->compilation_zone();
    auto* function_info_lowered = zone->New<compiler::FrameStateFunctionInfo>(
        compiler::FrameStateType::kLiftoffFunction, lowered_parameter_count,
        function_info->max_arguments(), lowered_local_count,
        function_info->shared_info(), function_info->wasm_liftoff_frame_size(),
        function_info->wasm_function_index());
    const FrameStateInfo& frame_state_info = data->frame_state_info;
    auto* frame_state_info_lowered = zone->New<compiler::FrameStateInfo>(
        frame_state_info.bailout_id(), frame_state_info.state_combine(),
        function_info_lowered);

    return Next::ReduceFrameState(
        builder.Inputs(), builder.inlined(),
        builder.AllocateFrameStateData(*frame_state_info_lowered, zone));
  }

 private:
  bool CheckPairOrPairOp(OpIndex input) {
#ifdef DEBUG
    if (const TupleOp* tuple = matcher_.TryCast<TupleOp>(input)) {
      DCHECK_EQ(2, tuple->input_count);
      RegisterRepresentation word32 = RegisterRepresentation::Word32();
      DCHECK(ValidOpInputRep(__ output_graph(), tuple->input(0), word32));
      DCHECK(ValidOpInputRep(__ output_graph(), tuple->input(1), word32));
    } else if (const DidntThrowOp* didnt_throw =
                   matcher_.TryCast<DidntThrowOp>(input)) {
      // If it's a call, it must be a call that returns exactly one i64.
      // (Note that the CallDescriptor has already been lowered to [i32, i32].)
      const CallOp& call =
          __ Get(didnt_throw->throwing_operation()).template Cast<CallOp>();
      DCHECK_EQ(call.descriptor->descriptor->ReturnCount(), 2);
      DCHECK_EQ(call.descriptor->descriptor->GetReturnType(0),
                MachineType::Int32());
      DCHECK_EQ(call.descriptor->descriptor->GetReturnType(1),
                MachineType::Int32());
    } else {
      DCHECK(matcher_.Is<Word32PairBinopOp>(input));
    }
#endif
    return true;
  }

  std::pair<V<Word32>, V<Word32>> Unpack(V<Word64> input) {
    DCHECK(CheckPairOrPairOp(input));
    auto input_w32p = V<Tuple<Word32, Word32>>::Cast(input);
    return {__ template Projection<0>(input_w32p),
            __ template Projection<1>(input_w32p)};
  }

  OpIndex LowerSignExtend(V<Word32> input) {
    // We use SAR to preserve the sign in the high word.
    return __ Tuple(input, __ Word32ShiftRightArithmetic(input, 31));
  }

  OpIndex LowerClz(V<Word64> input) {
    auto [low, high] = Unpack(input);
    ScopedVar<Word32> result(this);
    IF (__ Word32Equal(high, 0)) {
      result = __ Word32Add(32, __ Word32CountLeadingZeros(low));
    } ELSE {
      result = __ Word32CountLeadingZeros(high);
    }

    return __ Tuple(result, __ Word32Constant(0));
  }

  OpIndex LowerCtz(V<Word64> input) {
    DCHECK(SupportedOperations::word32_ctz());
    auto [low, high] = Unpack(input);
    ScopedVar<Word32> result(this);
    IF (__ Word32Equal(low, 0)) {
      result = __ Word32Add(32, __ Word32CountTrailingZeros(high));
    } ELSE {
      result = __ Word32CountTrailingZeros(low);
    }

    return __ Tuple(result, __ Word32Constant(0));
  }

  OpIndex LowerPopCount(V<Word64> input) {
    DCHECK(SupportedOperations::word32_popcnt());
    auto [low, high] = Unpack(input);
    return __ Tuple(
        __ Word32Add(__ Word32PopCount(low), __ Word32PopCount(high)),
        __ Word32Constant(0));
  }

  OpIndex LowerPairBinOp(V<Word64> left, V<Word64> right,
                         Word32PairBinopOp::Kind kind) {
    auto [left_low, left_high] = Unpack(left);
    auto [right_low, right_high] = Unpack(right);
    return __ Word32PairBinop(left_low, left_high, right_low, right_high, kind);
  }

  OpIndex LowerPairShiftOp(V<Word64> left, V<Word32> right,
                           Word32PairBinopOp::Kind kind) {
    auto [left_low, left_high] = Unpack(left);
    // Note: The rhs of a 64 bit shift is a 32 bit value in turboshaft.
    V<Word32> right_high = __ Word32Constant(0);
    return __ Word32PairBinop(left_low, left_high, right, right_high, kind);
  }

  OpIndex LowerBitwiseAnd(V<Word64> left, V<Word64> right) {
    auto [left_low, left_high] = Unpack(left);
    auto [right_low, right_high] = Unpack(right);
    V<Word32> low_result = __ Word32BitwiseAnd(left_low, right_low);
    V<Word32> high_result = __ Word32BitwiseAnd(left_high, right_high);
    return __ Tuple(low_result, high_result);
  }

  OpIndex LowerBitwiseOr(V<Word64> left, V<Word64> right) {
    auto [left_low, left_high] = Unpack(left);
    auto [right_low, right_high] = Unpack(right);
    V<Word32> low_result = __ Word32BitwiseOr(left_low, right_low);
    V<Word32> high_result = __ Word32BitwiseOr(left_high, right_high);
    return __ Tuple(low_result, high_result);
  }

  OpIndex LowerBitwiseXor(V<Word64> left, V<Word64> right) {
    auto [left_low, left_high] = Unpack(left);
    auto [right_low, right_high] = Unpack(right);
    V<Word32> low_result = __ Word32BitwiseXor(left_low, right_low);
    V<Word32> high_result = __ Word32BitwiseXor(left_high, right_high);
    return __ Tuple(low_result, high_result);
  }

  OpIndex LowerRotateRight(V<Word64> left, V<Word32> right) {
    // This reducer assumes that all rotates are mapped to rotate right.
    DCHECK(!SupportedOperations::word64_rol());
    auto [left_low, left_high] = Unpack(left);
    V<Word32> shift = right;
    uint32_t constant_shift = 0;

    if (matcher_.MatchIntegralWord32Constant(shift, &constant_shift)) {
      // Precondition: 0 <= shift < 64.
      uint32_t shift_value = constant_shift & 0x3F;
      if (shift_value == 0) {
        // No-op, return original tuple.
        return left;
      }
      if (shift_value == 32) {
        // Swap low and high of left.
        return __ Tuple(left_high, left_low);
      }

      V<Word32> low_input = left_high;
      V<Word32> high_input = left_low;
      if (shift_value < 32) {
        low_input = left_low;
        high_input = left_high;
      }

      uint32_t masked_shift_value = shift_value & 0x1F;
      V<Word32> masked_shift = __ Word32Constant(masked_shift_value);
      V<Word32> inv_shift = __ Word32Constant(32 - masked_shift_value);

      V<Word32> low_node = __ Word32BitwiseOr(
          __ Word32ShiftRightLogical(low_input, masked_shift),
          __ Word32ShiftLeft(high_input, inv_shift));
      V<Word32> high_node = __ Word32BitwiseOr(
          __ Word32ShiftRightLogical(high_input, masked_shift),
          __ Word32ShiftLeft(low_input, inv_shift));
      return __ Tuple(low_node, high_node);
    }

    V<Word32> safe_shift = shift;
    if (!SupportedOperations::word32_shift_is_safe()) {
      // safe_shift = shift % 32
      safe_shift = __ Word32BitwiseAnd(shift, 0x1F);
    }
    V<Word32> all_bits_set = __ Word32Constant(-1);
    V<Word32> inv_mask = __ Word32BitwiseXor(
        __ Word32ShiftRightLogical(all_bits_set, safe_shift), all_bits_set);
    V<Word32> bit_mask = __ Word32BitwiseXor(inv_mask, all_bits_set);

    V<Word32> less_than_32 = __ Int32LessThan(shift, 32);
    // The low word and the high word can be swapped either at the input or
    // at the output. We swap the inputs so that shift does not have to be
    // kept for so long in a register.
    ScopedVar<Word32> var_low(this, left_high);
    ScopedVar<Word32> var_high(this, left_low);
    IF (less_than_32) {
      var_low = left_low;
      var_high = left_high;
    }

    V<Word32> rotate_low = __ Word32RotateRight(var_low, safe_shift);
    V<Word32> rotate_high = __ Word32RotateRight(var_high, safe_shift);

    V<Word32> low_node =
        __ Word32BitwiseOr(__ Word32BitwiseAnd(rotate_low, bit_mask),
                           __ Word32BitwiseAnd(rotate_high, inv_mask));
    V<Word32> high_node =
        __ Word32BitwiseOr(__ Word32BitwiseAnd(rotate_high, bit_mask),
                           __ Word32BitwiseAnd(rotate_low, inv_mask));
    return __ Tuple(low_node, high_node);
  }

  V<Any> LowerCall(V<CallTarget> callee, OptionalV<FrameState> frame_state,
                   base::Vector<const OpIndex> arguments,
                   const TSCallDescriptor* descriptor, OpEffects effects,
                   bool is_tail_call) {
    // Iterate over the call descriptor to skip lowering if the signature does
    // not contain an i64.
    const CallDescriptor* call_descriptor = descriptor->descriptor;
    size_t param_count = call_descriptor->ParameterCount();
    size_t i64_params = 0;
    for (size_t i = 0; i < param_count; ++i) {
      i64_params += call_descriptor->GetParameterType(i).representation() ==
                    MachineRepresentation::kWord64;
    }
    size_t return_count = call_descriptor->ReturnCount();
    size_t i64_returns = 0;
    for (size_t i = 0; i < return_count; ++i) {
      i64_returns += call_descriptor->GetReturnType(i).representation() ==
                     MachineRepresentation::kWord64;
    }
    if (i64_params + i64_returns == 0) {
      // No lowering required.
      return is_tail_call ? Next::ReduceTailCall(callee, arguments, descriptor)
                          : Next::ReduceCall(callee, frame_state, arguments,
                                             descriptor, effects);
    }

    // Transform the BigIntToI64 call descriptor into BigIntToI32Pair (this is
    // the only use case currently, it may be extended in the future).
    // The correct target is already set during graph building.
    CallDescriptor* maybe_special_replacement =
        wasm::GetWasmEngine()->call_descriptors()->GetLoweredCallDescriptor(
            call_descriptor);
    if (maybe_special_replacement) call_descriptor = maybe_special_replacement;
    // Create descriptor with 2 i32s for every i64.
    const CallDescriptor* lowered_descriptor =
        GetI32WasmCallDescriptor(__ graph_zone(), call_descriptor);

    // Map the arguments by unpacking i64 arguments (which have already been
    // lowered to Tuple(i32, i32).)
    base::SmallVector<OpIndex, 16> lowered_args;
    lowered_args.reserve(param_count + i64_params);

    DCHECK_EQ(param_count, arguments.size());
    for (size_t i = 0; i < param_count; ++i) {
      if (call_descriptor->GetParameterType(i).representation() ==
          MachineRepresentation::kWord64) {
        auto [low, high] = Unpack(arguments[i]);
        lowered_args.push_back(low);
        lowered_args.push_back(high);
      } else {
        lowered_args.push_back(arguments[i]);
      }
    }

    auto lowered_ts_descriptor =
        TSCallDescriptor::Create(lowered_descriptor, descriptor->can_throw,
                                 LazyDeoptOnThrow::kNo, __ graph_zone());
    OpIndex call =
        is_tail_call
            ? Next::ReduceTailCall(callee, base::VectorOf(lowered_args),
                                   lowered_ts_descriptor)
            : Next::ReduceCall(callee, frame_state,
                               base::VectorOf(lowered_args),
                               lowered_ts_descriptor, effects);
    if (is_tail_call) {
      // Tail calls don't return anything to the calling function.
      return call;
    }
    if (i64_returns == 0 || return_count == 0) {
      return call;
    } else if (return_count == 1) {
      // There isn't any projection in the input graph for calls returning
      // exactly one value. Return a tuple of projections for the int64.
      DCHECK_EQ(i64_returns, 1);
      return call;
    }

    // Wrap the call node with a tuple of projections of the lowered call.
    // Example for a call returning [int64, int32]:
    //   In:  Call(...) -> [int64, int32]
    //   Out: call = Call() -> [int32, int32, int32]
    //        Tuple(
    //           Tuple(Projection(call, 0), Projection(call, 1)),
    //           Projection(call, 2))
    //
    // This way projections on the original call node will be automatically
    // "rewired" to the correct projection of the lowered call.
    auto word32 = RegisterRepresentation::Word32();
    base::SmallVector<V<Any>, 16> tuple_inputs;
    tuple_inputs.reserve(return_count);
    size_t projection_index = 0;  // index of the lowered call results.

    for (size_t i = 0; i < return_count; ++i) {
      MachineRepresentation machine_rep =
          call_descriptor->GetReturnType(i).representation();
      if (machine_rep == MachineRepresentation::kWord64) {
        tuple_inputs.push_back(
            __ Tuple(__ Projection(call, projection_index, word32),
                     __ Projection(call, projection_index + 1, word32)));
        projection_index += 2;
      } else {
        tuple_inputs.push_back(__ Projection(
            call, projection_index++,
            RegisterRepresentation::FromMachineRepresentation(machine_rep)));
      }
    }
    DCHECK_EQ(projection_index, return_count + i64_returns);
    return __ Tuple(base::VectorOf(tuple_inputs));
  }

  void InitializeIndexMaps() {
    // Add one implicit parameter in front.
    param_index_map_.push_back(0);
    int32_t new_index = 0;
    for (size_t i = 0; i < sig_->parameter_count(); ++i) {
      param_index_map_.push_back(++new_index);
      if (sig_->GetParam(i) == MachineRepresentation::kWord64) {
        // i64 becomes [i32 low, i32 high], so the next parameter index is
        // shifted by one.
        ++new_index;
      }
    }

    returns_i64_ = std::any_of(sig_->returns().begin(), sig_->returns().end(),
                               [](const MachineRepresentation rep) {
                                 return rep == MachineRepresentation::kWord64;
                               });
  }

  const Signature<MachineRepresentation>* sig_;
  Zone* zone_ = __ graph_zone();
  ZoneVector<int32_t> param_index_map_{__ phase_zone()};
  bool returns_i64_ = false;  // Returns at least one i64.
  const OperationMatcher& matcher_{__ matcher()};
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_INT64_LOWERING_REDUCER_H_
