// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_GRAPH_VISUALIZER_H_
#define V8_COMPILER_TURBOSHAFT_GRAPH_VISUALIZER_H_

#include "src/common/globals.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/handles/handles.h"

namespace v8::internal::compiler::turboshaft {

struct TurboshaftGraphAsJSON {
  const Graph& turboshaft_graph;
  NodeOriginTable* origins;
  Zone* temp_zone;
};

V8_INLINE V8_EXPORT_PRIVATE TurboshaftGraphAsJSON
AsJSON(const Graph& graph, NodeOriginTable* origins, Zone* temp_zone) {
  return TurboshaftGraphAsJSON{graph, origins, temp_zone};
}

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const TurboshaftGraphAsJSON& ad);

class JSONTurboshaftGraphWriter {
 public:
  JSONTurboshaftGraphWriter(std::ostream& os, const Graph& turboshaft_graph,
                            NodeOriginTable* origins, Zone* zone);

  JSONTurboshaftGraphWriter(const JSONTurboshaftGraphWriter&) = delete;
  JSONTurboshaftGraphWriter& operator=(const JSONTurboshaftGraphWriter&) =
      delete;

  void Print();

 protected:
  void PrintNodes();
  void PrintEdges();
  void PrintBlocks();

 protected:
  std::ostream& os_;
  Zone* zone_;
  const Graph& turboshaft_graph_;
  NodeOriginTable* origins_;
};

void PrintTurboshaftCustomDataPerOperation(
    std::ofstream& stream, const char* data_name, const Graph& graph,
    std::function<bool(std::ostream&, const Graph&, OpIndex)> printer);
void PrintTurboshaftCustomDataPerBlock(
    std::ofstream& stream, const char* data_name, const Graph& graph,
    std::function<bool(std::ostream&, const Graph&, BlockIndex)> printer);

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_GRAPH_VISUALIZER_H_
