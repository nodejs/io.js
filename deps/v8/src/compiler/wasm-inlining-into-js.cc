// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-inlining-into-js.h"

#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/wasm-compiler-definitions.h"
#include "src/compiler/wasm-compiler.h"
#include "src/compiler/wasm-graph-assembler.h"
#include "src/wasm/decoder.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-opcodes-inl.h"
#include "src/wasm/wasm-subtyping.h"

namespace v8::internal::compiler {

namespace {

using wasm::WasmOpcode;
using wasm::WasmOpcodes;

class WasmIntoJSInlinerImpl : private wasm::Decoder {
  using ValidationTag = NoValidationTag;

  struct Value {
    Node* node = nullptr;
    wasm::ValueType type = wasm::kWasmBottom;
  };

 public:
  WasmIntoJSInlinerImpl(Zone* zone, const wasm::WasmModule* module,
                        MachineGraph* mcgraph, const wasm::FunctionBody& body,
                        base::Vector<const uint8_t> bytes,
                        SourcePositionTable* source_position_table,
                        int inlining_id)
      : wasm::Decoder(bytes.begin(), bytes.end()),
        module_(module),
        mcgraph_(mcgraph),
        body_(body),
        graph_(mcgraph->graph()),
        gasm_(mcgraph, zone),
        source_position_table_(source_position_table),
        inlining_id_(inlining_id) {
    // +1 for instance node.
    size_t params = body.sig->parameter_count() + 1;
    Node* start =
        graph_->NewNode(mcgraph->common()->Start(static_cast<int>(params)));
    graph_->SetStart(start);
    graph_->SetEnd(graph_->NewNode(mcgraph->common()->End(0)));
    gasm_.InitializeEffectControl(start, start);

    // Initialize parameter nodes.
    // We have to add another +1 as the minimum parameter index is actually
    // -1, not 0...
    size_t params_extended = params + 1;
    parameters_ = zone->AllocateArray<Node*>(params_extended);
    for (unsigned i = 0; i < params_extended; i++) {
      parameters_[i] = nullptr;
    }
    // Instance node at parameter 0.
    trusted_data_node_ = Param(wasm::kWasmInstanceParameterIndex);
  }

  Node* Param(int index, const char* debug_name = nullptr) {
    DCHECK_NOT_NULL(graph_->start());
    // Turbofan allows negative parameter indices.
    DCHECK_GE(index, kMinParameterIndex);
    int array_index = index - kMinParameterIndex;
    if (parameters_[array_index] == nullptr) {
      Node* param = graph_->NewNode(
          mcgraph_->common()->Parameter(index, debug_name), graph_->start());
      if (index > wasm::kWasmInstanceParameterIndex) {
        // Add a type guard to keep type information based on the inlinee's
        // signature.
        wasm::ValueType type = body_.sig->GetParam(index - 1);
        Type tf_type = compiler::Type::Wasm(type, module_, graph_->zone());
        param = gasm_.TypeGuard(tf_type, param);
      }
      parameters_[array_index] = param;
    }
    return parameters_[array_index];
  }

  bool TryInlining() {
    if (body_.sig->return_count() > 1) {
      return false;  // Multi-return is not supported.
    }
    // Parse locals.
    if (consume_u32v() != 0) {
      // Functions with locals are not supported.
      return false;
    }
    // Parse body.
    base::SmallVector<Value, 4> stack;
    while (is_inlineable_) {
      WasmOpcode opcode = ReadOpcode();
      switch (opcode) {
        case wasm::kExprAnyConvertExtern:
          DCHECK(!stack.empty());
          stack.back() = ParseAnyConvertExtern(stack.back());
          continue;
        case wasm::kExprExternConvertAny:
          DCHECK(!stack.empty());
          stack.back() = ParseExternConvertAny(stack.back());
          continue;
        case wasm::kExprRefCast:
        case wasm::kExprRefCastNull:
          DCHECK(!stack.empty());
          stack.back() =
              ParseRefCast(stack.back(), opcode == wasm::kExprRefCastNull);
          continue;
        case wasm::kExprArrayLen:
          DCHECK(!stack.empty());
          stack.back() = ParseArrayLen(stack.back());
          continue;
        case wasm::kExprArrayGet:
        case wasm::kExprArrayGetS:
        case wasm::kExprArrayGetU: {
          DCHECK_GE(stack.size(), 2);
          Value index = stack.back();
          stack.pop_back();
          Value array = stack.back();
          stack.back() = ParseArrayGet(array, index, opcode);
          continue;
        }
        case wasm::kExprArraySet: {
          DCHECK_GE(stack.size(), 3);
          Value value = stack.back();
          stack.pop_back();
          Value index = stack.back();
          stack.pop_back();
          Value array = stack.back();
          stack.pop_back();
          ParseArraySet(array, index, value);
          continue;
        }
        case wasm::kExprStructGet:
        case wasm::kExprStructGetS:
        case wasm::kExprStructGetU:
          DCHECK(!stack.empty());
          stack.back() = ParseStructGet(stack.back(), opcode);
          continue;
        case wasm::kExprStructSet: {
          DCHECK_GE(stack.size(), 2);
          Value value = stack.back();
          stack.pop_back();
          Value wasm_struct = stack.back();
          stack.pop_back();
          ParseStructSet(wasm_struct, value);
          continue;
        }
        case wasm::kExprLocalGet:
          stack.push_back(ParseLocalGet());
          continue;
        case wasm::kExprDrop:
          DCHECK(!stack.empty());
          stack.pop_back();
          continue;
        case wasm::kExprEnd: {
          DCHECK_LT(stack.size(), 2);
          int return_count = static_cast<int>(stack.size());
          base::SmallVector<Node*, 8> buf(return_count + 3);
          buf[0] = mcgraph_->Int32Constant(0);
          if (return_count) {
            buf[1] = stack.back().node;
          }
          buf[return_count + 1] = gasm_.effect();
          buf[return_count + 2] = gasm_.control();
          Node* ret = graph_->NewNode(mcgraph_->common()->Return(return_count),
                                      return_count + 3, buf.data());

          gasm_.MergeControlToEnd(ret);
          return true;
        }
        default:
          // Instruction not supported for inlining.
          return false;
      }
    }
    // The decoder found an instruction it couldn't inline successfully.
    return false;
  }

 private:
  Value ParseAnyConvertExtern(Value input) {
    DCHECK(input.type.is_reference_to(wasm::HeapType::kExtern) ||
           input.type.is_reference_to(wasm::HeapType::kNoExtern));
    wasm::ValueType result_type = wasm::ValueType::RefMaybeNull(
        wasm::HeapType::kAny, input.type.is_nullable()
                                  ? wasm::Nullability::kNullable
                                  : wasm::Nullability::kNonNullable);
    Node* internalized = gasm_.WasmAnyConvertExtern(input.node);
    return TypeNode(internalized, result_type);
  }

  Value ParseExternConvertAny(Value input) {
    DCHECK(input.type.is_reference());
    wasm::ValueType result_type = wasm::ValueType::RefMaybeNull(
        wasm::HeapType::kExtern, input.type.is_nullable()
                                     ? wasm::Nullability::kNullable
                                     : wasm::Nullability::kNonNullable);
    Node* internalized = gasm_.WasmExternConvertAny(input.node);
    return TypeNode(internalized, result_type);
  }

  Value ParseLocalGet() {
    uint32_t index = consume_u32v();
    DCHECK_LT(index, body_.sig->parameter_count());
    return TypeNode(Param(index + 1), body_.sig->GetParam(index));
  }

  Value ParseStructGet(Value struct_val, WasmOpcode opcode) {
    uint32_t struct_index = consume_u32v();
    DCHECK(module_->has_struct(struct_index));
    const wasm::StructType* struct_type = module_->struct_type(struct_index);
    uint32_t field_index = consume_u32v();
    DCHECK_GT(struct_type->field_count(), field_index);
    const bool is_signed = opcode == wasm::kExprStructGetS;
    const CheckForNull null_check =
        struct_val.type.is_nullable() ? kWithNullCheck : kWithoutNullCheck;
    Node* member = gasm_.StructGet(struct_val.node, struct_type, field_index,
                                   is_signed, null_check);
    SetSourcePosition(member);
    return TypeNode(member, struct_type->field(field_index).Unpacked());
  }

  void ParseStructSet(Value wasm_struct, Value value) {
    uint32_t struct_index = consume_u32v();
    DCHECK(module_->has_struct(struct_index));
    const wasm::StructType* struct_type = module_->struct_type(struct_index);
    uint32_t field_index = consume_u32v();
    DCHECK_GT(struct_type->field_count(), field_index);
    const CheckForNull null_check =
        wasm_struct.type.is_nullable() ? kWithNullCheck : kWithoutNullCheck;
    gasm_.StructSet(wasm_struct.node, value.node, struct_type, field_index,
                    null_check);
    SetSourcePosition(gasm_.effect());
  }

  Value ParseRefCast(Value input, bool null_succeeds) {
    auto [heap_index, length] = read_i33v<ValidationTag>(pc_);
    pc_ += length;
    if (heap_index < 0) {
      if ((heap_index & 0x7f) != wasm::kArrayRefCode) {
        // Abstract casts for non array type are not supported.
        is_inlineable_ = false;
        return {};
      }
      auto done = gasm_.MakeLabel();
      // Abstract cast to array.
      if (input.type.is_nullable() && null_succeeds) {
        gasm_.GotoIf(gasm_.IsNull(input.node, input.type), &done);
      }
      gasm_.TrapIf(gasm_.IsSmi(input.node), TrapId::kTrapIllegalCast);
      gasm_.TrapUnless(gasm_.HasInstanceType(input.node, WASM_ARRAY_TYPE),
                       TrapId::kTrapIllegalCast);
      SetSourcePosition(gasm_.effect());
      gasm_.Goto(&done);
      gasm_.Bind(&done);
      // Add TypeGuard for graph typing.
      Graph* graph = mcgraph_->graph();
      wasm::ValueType result_type = wasm::ValueType::RefMaybeNull(
          wasm::HeapType::kArray,
          null_succeeds ? wasm::kNullable : wasm::kNonNullable);
      Node* type_guard =
          graph->NewNode(mcgraph_->common()->TypeGuard(
                             Type::Wasm(result_type, module_, graph->zone())),
                         input.node, gasm_.effect(), gasm_.control());
      gasm_.InitializeEffectControl(type_guard, gasm_.control());
      return TypeNode(type_guard, result_type);
    }
    if (module_->has_signature(static_cast<uint32_t>(heap_index))) {
      is_inlineable_ = false;
      return {};
    }
    wasm::ValueType target_type = wasm::ValueType::RefMaybeNull(
        static_cast<uint32_t>(heap_index),
        null_succeeds ? wasm::kNullable : wasm::kNonNullable);
    Node* rtt = mcgraph_->graph()->NewNode(
        gasm_.simplified()->RttCanon(target_type.ref_index()),
        trusted_data_node_);
    TypeNode(rtt, wasm::ValueType::Rtt(target_type.ref_index()));
    Node* cast = gasm_.WasmTypeCast(input.node, rtt, {input.type, target_type});
    SetSourcePosition(cast);
    return TypeNode(cast, target_type);
  }

  Value ParseArrayLen(Value input) {
    DCHECK(wasm::IsHeapSubtypeOf(input.type.heap_type(),
                                 wasm::HeapType(wasm::HeapType::kArray),
                                 module_));
    const CheckForNull null_check =
        input.type.is_nullable() ? kWithNullCheck : kWithoutNullCheck;
    Node* len = gasm_.ArrayLength(input.node, null_check);
    SetSourcePosition(len);
    return TypeNode(len, wasm::kWasmI32);
  }

  Value ParseArrayGet(Value array, Value index, WasmOpcode opcode) {
    uint32_t array_index = consume_u32v();
    DCHECK(module_->has_array(array_index));
    const wasm::ArrayType* array_type = module_->array_type(array_index);
    const bool is_signed = opcode == WasmOpcode::kExprArrayGetS;
    const CheckForNull null_check =
        array.type.is_nullable() ? kWithNullCheck : kWithoutNullCheck;
    // Perform bounds check.
    Node* length = gasm_.ArrayLength(array.node, null_check);
    SetSourcePosition(length);
    gasm_.TrapUnless(gasm_.Uint32LessThan(index.node, length),
                     TrapId::kTrapArrayOutOfBounds);
    SetSourcePosition(gasm_.effect());
    // Perform array.get.
    Node* element =
        gasm_.ArrayGet(array.node, index.node, array_type, is_signed);
    return TypeNode(element, array_type->element_type().Unpacked());
  }

  void ParseArraySet(Value array, Value index, Value value) {
    uint32_t array_index = consume_u32v();
    DCHECK(module_->has_array(array_index));
    const wasm::ArrayType* array_type = module_->array_type(array_index);
    const CheckForNull null_check =
        array.type.is_nullable() ? kWithNullCheck : kWithoutNullCheck;
    // Perform bounds check.
    Node* length = gasm_.ArrayLength(array.node, null_check);
    SetSourcePosition(length);
    gasm_.TrapUnless(gasm_.Uint32LessThan(index.node, length),
                     TrapId::kTrapArrayOutOfBounds);
    SetSourcePosition(gasm_.effect());
    // Perform array.set.
    gasm_.ArraySet(array.node, index.node, value.node, array_type);
  }

  WasmOpcode ReadOpcode() {
    DCHECK_LT(pc_, end_);
    instruction_start_ = pc();
    WasmOpcode opcode = static_cast<WasmOpcode>(*pc_);
    if (!WasmOpcodes::IsPrefixOpcode(opcode)) {
      ++pc_;
      return opcode;
    }
    auto [opcode_with_prefix, length] =
        read_prefixed_opcode<ValidationTag>(pc_);
    pc_ += length;
    return opcode_with_prefix;
  }

  Value TypeNode(Node* node, wasm::ValueType type) {
    compiler::NodeProperties::SetType(
        node, compiler::Type::Wasm(type, module_, graph_->zone()));
    return {node, type};
  }

  void SetSourcePosition(Node* node) {
    if (!source_position_table_->IsEnabled()) return;
    int offset = static_cast<int>(instruction_start_ - start());
    source_position_table_->SetSourcePosition(
        node, SourcePosition(offset, inlining_id_));
  }

  const wasm::WasmModule* module_;
  MachineGraph* mcgraph_;
  const wasm::FunctionBody& body_;
  Node** parameters_;
  Graph* graph_;
  Node* trusted_data_node_;
  WasmGraphAssembler gasm_;
  SourcePositionTable* source_position_table_ = nullptr;
  const uint8_t* instruction_start_ = pc_;
  int inlining_id_;
  bool is_inlineable_ = true;
};

}  // anonymous namespace

bool WasmIntoJSInliner::TryInlining(Zone* zone, const wasm::WasmModule* module,
                                    MachineGraph* mcgraph,
                                    const wasm::FunctionBody& body,
                                    base::Vector<const uint8_t> bytes,
                                    SourcePositionTable* source_position_table,
                                    int inlining_id) {
  WasmIntoJSInlinerImpl inliner(zone, module, mcgraph, body, bytes,
                                source_position_table, inlining_id);
  return inliner.TryInlining();
}

}  // namespace v8::internal::compiler
