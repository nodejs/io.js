// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_CONST_TRACKING_LET_HELPERS_H_
#define V8_COMPILER_CONST_TRACKING_LET_HELPERS_H_

#include <stddef.h>

#include "src/compiler/heap-refs.h"

namespace v8::internal::compiler {

class HeapBroker;
class JSGraph;
class Node;

int ConstTrackingLetSideDataIndexForAccess(size_t access_index);

void GenerateCheckConstTrackingLetSideData(Node* context, Node** effect,
                                           Node** control, int side_data_index,
                                           JSGraph* jsgraph);

bool IsConstTrackingLetVariableSurelyNotConstant(
    OptionalContextRef maybe_context, size_t depth, int side_data_index,
    JSHeapBroker* broker);

}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_CONST_TRACKING_LET_HELPERS_H_
