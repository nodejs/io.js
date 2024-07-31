// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/const-tracking-let-helpers.h"

#include "src/compiler/access-builder.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node.h"
#include "src/compiler/simplified-operator.h"
#include "src/objects/property-cell.h"

namespace v8::internal::compiler {

int ConstTrackingLetSideDataIndexForAccess(size_t access_index) {
  return static_cast<int>(access_index) - Context::MIN_CONTEXT_EXTENDED_SLOTS;
}

void GenerateCheckConstTrackingLetSideData(Node* context, Node** effect,
                                           Node** control, int side_data_index,
                                           JSGraph* jsgraph) {
  Node* side_data = *effect = jsgraph->graph()->NewNode(
      jsgraph->simplified()->LoadField(AccessBuilder::ForContextSlot(
          Context::CONST_TRACKING_LET_SIDE_DATA_INDEX)),
      context, *effect, *control);
  Node* side_data_value = *effect = jsgraph->graph()->NewNode(
      jsgraph->simplified()->LoadField(
          AccessBuilder::ForFixedArraySlot(side_data_index)),
      side_data, *effect, *control);

  // TODO(v8:13567): If the value is the same as the value we already have, we
  // don't need to deopt.

  // Deoptimize if the side_data_value is something else than the "not a
  // constant" sentinel: the value might be a constant and something might
  // depend on it.
  static_assert(ConstTrackingLetCell::kNonConstMarker.value() == 0);
  Node* check =
      jsgraph->graph()->NewNode(jsgraph->simplified()->ReferenceEqual(),
                                side_data_value, jsgraph->ZeroConstant());
  *effect = jsgraph->graph()->NewNode(
      jsgraph->simplified()->CheckIf(DeoptimizeReason::kConstTrackingLet),
      check, *effect, *control);
}

bool IsConstTrackingLetVariableSurelyNotConstant(
    OptionalContextRef maybe_context, size_t depth, int side_data_index,
    JSHeapBroker* broker) {
  if (maybe_context.has_value() && depth == 0) {
    ContextRef context = maybe_context.value();
    OptionalObjectRef side_data =
        context.get(broker, Context::CONST_TRACKING_LET_SIDE_DATA_INDEX);
    if (side_data.has_value()) {
      OptionalObjectRef side_data_value =
          side_data->AsFixedArray().TryGet(broker, side_data_index);
      if (side_data_value.has_value()) {
        auto value = side_data_value.value();
        if (value.IsSmi() &&
            value.AsSmi() ==
                Smi::ToInt(ConstTrackingLetCell::kNonConstMarker)) {
          // The value is not a constant any more.
          return true;
        }
      }
    }
  }
  // Either the value is not a constant, or we don't know.
  return false;
}

}  // namespace v8::internal::compiler
