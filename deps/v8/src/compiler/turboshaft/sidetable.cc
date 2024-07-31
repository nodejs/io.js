// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/sidetable.h"

#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"

namespace v8::internal::compiler::turboshaft {

#ifdef DEBUG
bool OpIndexBelongsToTableGraph(const Graph* graph, OpIndex index) {
  return graph->BelongsToThisGraph(index);
}
#endif

}  // namespace v8::internal::compiler::turboshaft
