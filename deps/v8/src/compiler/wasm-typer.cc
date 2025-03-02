// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-typer.h"

#include "src/base/logging.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/wasm-compiler-definitions.h"
#include "src/utils/utils.h"
#include "src/wasm/object-access.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-subtyping.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE(...) \
  if (v8_flags.trace_wasm_typer) PrintF(__VA_ARGS__);

WasmTyper::WasmTyper(Editor* editor, MachineGraph* mcgraph,
                     uint32_t function_index)
    : AdvancedReducer(editor),
      function_index_(function_index),
      graph_zone_(mcgraph->graph()->zone()) {}

namespace {
bool AllInputsTyped(Node* node) {
  for (int i = 0; i < node->op()->ValueInputCount(); i++) {
    if (!NodeProperties::IsTyped(NodeProperties::GetValueInput(node, i))) {
      return false;
    }
  }
  return true;
}
}  // namespace

Reduction WasmTyper::Reduce(Node* node) {
  using TypeInModule = wasm::TypeInModule;
  TypeInModule computed_type;
  switch (node->opcode()) {
    case IrOpcode::kTypeGuard: {
      if (!AllInputsTyped(node)) return NoChange();
      Type guarded_type = TypeGuardTypeOf(node->op());
      if (!guarded_type.IsWasm()) return NoChange();
      Type input_type =
          NodeProperties::GetType(NodeProperties::GetValueInput(node, 0));
      if (!input_type.IsWasm()) return NoChange();
      TypeInModule guarded_wasm_type = guarded_type.AsWasm();
      TypeInModule input_wasm_type = input_type.AsWasm();
      // Note: The intersection type might be bottom. In this case, we are in a
      // dead branch: Type this node as bottom and wait for the
      // WasmGCOperatorReducer to remove it.
      computed_type = wasm::Intersection(guarded_wasm_type, input_wasm_type);
      break;
    }
    case IrOpcode::kWasmTypeCast:
    case IrOpcode::kWasmTypeCastAbstract: {
      if (!AllInputsTyped(node)) return NoChange();
      TypeInModule object_type =
          NodeProperties::GetType(NodeProperties::GetValueInput(node, 0))
              .AsWasm();
      wasm::ValueType to_type = OpParameter<WasmTypeCheckConfig>(node->op()).to;
      // TODO(12166): Change module parameters if we have cross-module inlining.
      computed_type = wasm::Intersection(
          object_type.type, to_type, object_type.module, object_type.module);
      break;
    }
    case IrOpcode::kAssertNotNull: {
      if (!AllInputsTyped(node)) return NoChange();
      TypeInModule object_type =
          NodeProperties::GetType(NodeProperties::GetValueInput(node, 0))
              .AsWasm();
      computed_type = {object_type.type.AsNonNull(), object_type.module};
      break;
    }
    case IrOpcode::kPhi: {
      if (!AllInputsTyped(node)) {
        bool is_loop_phi =
            NodeProperties::GetControlInput(node)->opcode() == IrOpcode::kLoop;
        // For a merge phi, we need all inputs to be typed.
        if (!is_loop_phi) return NoChange();
        // For a loop phi, we can forward the non-recursive-input type. We can
        // recompute the type when the rest of the inputs' types are computed.
        Node* non_recursive_input = NodeProperties::GetValueInput(node, 0);
        if (!NodeProperties::IsTyped(non_recursive_input) ||
            !NodeProperties::GetType(non_recursive_input).IsWasm()) {
          return NoChange();
        }
        computed_type = NodeProperties::GetType(non_recursive_input).AsWasm();
        TRACE("function: %d, loop phi node: %d, type: %s\n", function_index_,
              node->id(), computed_type.type.name().c_str());
        break;
      }

      Type first_input_type =
          NodeProperties::GetType(NodeProperties::GetValueInput(node, 0));
      if (!first_input_type.IsWasm()) return NoChange();
      computed_type = first_input_type.AsWasm();
      for (int i = 1; i < node->op()->ValueInputCount(); i++) {
        Node* input = NodeProperties::GetValueInput(node, i);
        Type input_type = NodeProperties::GetType(input);
        if (!input_type.IsWasm()) return NoChange();
        TypeInModule wasm_type = input_type.AsWasm();
        if (computed_type.type.is_bottom()) {
          // We have not found a non-bottom branch yet.
          computed_type = wasm_type;
        } else if (!wasm_type.type.is_bottom()) {
          // We do not want union of types from unreachable branches.
          computed_type = wasm::Union(computed_type, wasm_type);
        }
      }
      TRACE(
          "function: %d, phi node: %d, input#: %d, input0:%d:%s, input1:%d:%s, "
          "type: %s\n",
          function_index_, node->id(), node->op()->ValueInputCount(),
          node->InputAt(0)->id(),
          NodeProperties::GetType(node->InputAt(0))
              .AsWasm()
              .type.name()
              .c_str(),
          node->InputAt(1)->id(),
          node->op()->ValueInputCount() > 1
              ? NodeProperties::GetType(node->InputAt(1))
                    .AsWasm()
                    .type.name()
                    .c_str()
              : "<control>",
          computed_type.type.name().c_str());
      break;
    }
    case IrOpcode::kWasmArrayGet: {
      Node* object = NodeProperties::GetValueInput(node, 0);
      // This can happen either because the object has not been typed yet, or
      // because it is an internal VM object (e.g. the instance).
      if (!NodeProperties::IsTyped(object)) return NoChange();
      TypeInModule object_type = NodeProperties::GetType(object).AsWasm();
      // {is_uninhabited} can happen in unreachable branches.
      if (object_type.type.is_uninhabited() ||
          object_type.type == wasm::kWasmNullRef) {
        computed_type = {wasm::kWasmBottom, object_type.module};
        break;
      }
      wasm::ModuleTypeIndex ref_index = object_type.type.ref_index();
      DCHECK(object_type.module->has_array(ref_index));
      const wasm::ArrayType* type_from_object =
          object_type.module->type(ref_index).array_type;
      computed_type = {type_from_object->element_type().Unpacked(),
                       object_type.module};
      break;
    }
    case IrOpcode::kWasmStructGet: {
      Node* object = NodeProperties::GetValueInput(node, 0);
      // This can happen either because the object has not been typed yet.
      if (!NodeProperties::IsTyped(object)) return NoChange();
      TypeInModule object_type = NodeProperties::GetType(object).AsWasm();
      // {is_uninhabited} can happen in unreachable branches.
      if (object_type.type.is_uninhabited() ||
          object_type.type == wasm::kWasmNullRef) {
        computed_type = {wasm::kWasmBottom, object_type.module};
        break;
      }
      WasmFieldInfo info = OpParameter<WasmFieldInfo>(node->op());

      wasm::ModuleTypeIndex ref_index = object_type.type.ref_index();

      DCHECK(object_type.module->has_struct(ref_index));

      const wasm::StructType* struct_type_from_object =
          object_type.module->type(ref_index).struct_type;

      computed_type = {
          struct_type_from_object->field(info.field_index).Unpacked(),
          object_type.module};
      break;
    }
    case IrOpcode::kNull: {
      TypeInModule from_node = NodeProperties::GetType(node).AsWasm();
      computed_type = {wasm::ToNullSentinel(from_node), from_node.module};
      break;
    }
    default:
      return NoChange();
  }

  if (NodeProperties::IsTyped(node) && NodeProperties::GetType(node).IsWasm()) {
    TypeInModule current_type = NodeProperties::GetType(node).AsWasm();
    if (!(current_type.type.is_bottom() || computed_type.type.is_bottom() ||
          wasm::IsSubtypeOf(current_type.type, computed_type.type,
                            current_type.module, computed_type.module) ||
          wasm::IsSubtypeOf(computed_type.type, current_type.type,
                            computed_type.module, current_type.module) ||
          // Imported strings can have more precise types.
          (current_type.type.heap_representation() == wasm::HeapType::kExtern &&
           computed_type.type.heap_representation() ==
               wasm::HeapType::kString))) {
      FATAL(
          "Error - Incompatible types. function: %d, node: %d:%s, input0:%d, "
          "current %s, computed %s\n",
          function_index_, node->id(), node->op()->mnemonic(),
          node->InputAt(0)->id(), current_type.type.name().c_str(),
          computed_type.type.name().c_str());
    }

    if (wasm::EquivalentTypes(current_type.type, computed_type.type,
                              current_type.module, computed_type.module)) {
      return NoChange();
    }
  }

  TRACE("function: %d, node: %d:%s, from: %s, to: %s\n", function_index_,
        node->id(), node->op()->mnemonic(),
        NodeProperties::IsTyped(node)
            ? NodeProperties::GetType(node).AsWasm().type.name().c_str()
            : "<untyped>",
        computed_type.type.name().c_str());

  NodeProperties::SetType(node, Type::Wasm(computed_type, graph_zone_));
  return Changed(node);
}

#undef TRACE

}  // namespace compiler
}  // namespace internal
}  // namespace v8
