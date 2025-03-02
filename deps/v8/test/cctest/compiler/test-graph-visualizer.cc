// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/node.h"
#include "src/compiler/operator.h"
#include "src/compiler/schedule.h"
#include "src/compiler/scheduler.h"
#include "src/compiler/turbofan-graph-visualizer.h"
#include "src/compiler/turbofan-graph.h"
#include "src/compiler/verifier.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace compiler {

static Operator dummy_operator1(IrOpcode::kParameter, Operator::kNoWrite,
                                "dummy", 1, 0, 0, 1, 0, 0);
static Operator dummy_operator6(IrOpcode::kParameter, Operator::kNoWrite,
                                "dummy", 6, 0, 0, 1, 0, 0);


TEST(NodeWithNullInputReachableFromEnd) {
  HandleAndZoneScope scope(kCompressGraphZone);
  Graph graph(scope.main_zone());
  CommonOperatorBuilder common(scope.main_zone());

  Node* start = graph.NewNode(common.Start(0));
  graph.SetStart(start);
  Node* k = graph.NewNode(common.Int32Constant(0));
  Node* phi =
      graph.NewNode(common.Phi(MachineRepresentation::kTagged, 1), k, start);
  phi->ReplaceInput(0, nullptr);
  graph.SetEnd(phi);

  SourcePositionTable table(&graph);
  NodeOriginTable table2(&graph);
  StdoutStream{} << AsJSON(graph, &table, &table2);
}


TEST(NodeWithNullControlReachableFromEnd) {
  HandleAndZoneScope scope(kCompressGraphZone);
  Graph graph(scope.main_zone());
  CommonOperatorBuilder common(scope.main_zone());

  Node* start = graph.NewNode(common.Start(0));
  graph.SetStart(start);
  Node* k = graph.NewNode(common.Int32Constant(0));
  Node* phi =
      graph.NewNode(common.Phi(MachineRepresentation::kTagged, 1), k, start);
  phi->ReplaceInput(1, nullptr);
  graph.SetEnd(phi);

  SourcePositionTable table(&graph);
  NodeOriginTable table2(&graph);
  StdoutStream{} << AsJSON(graph, &table, &table2);
}


TEST(NodeWithNullInputReachableFromStart) {
  HandleAndZoneScope scope(kCompressGraphZone);
  Graph graph(scope.main_zone());
  CommonOperatorBuilder common(scope.main_zone());

  Node* start = graph.NewNode(common.Start(0));
  graph.SetStart(start);
  Node* k = graph.NewNode(common.Int32Constant(0));
  Node* phi =
      graph.NewNode(common.Phi(MachineRepresentation::kTagged, 1), k, start);
  phi->ReplaceInput(0, nullptr);
  graph.SetEnd(start);

  SourcePositionTable table(&graph);
  NodeOriginTable table2(&graph);
  StdoutStream{} << AsJSON(graph, &table, &table2);
}


TEST(NodeWithNullControlReachableFromStart) {
  HandleAndZoneScope scope(kCompressGraphZone);
  Graph graph(scope.main_zone());
  CommonOperatorBuilder common(scope.main_zone());

  Node* start = graph.NewNode(common.Start(0));
  graph.SetStart(start);
  Node* merge = graph.NewNode(common.Merge(2), start, start);
  merge->ReplaceInput(1, nullptr);
  graph.SetEnd(merge);

  SourcePositionTable table(&graph);
  NodeOriginTable table2(&graph);
  StdoutStream{} << AsJSON(graph, &table, &table2);
}


TEST(NodeNetworkOfDummiesReachableFromEnd) {
  HandleAndZoneScope scope(kCompressGraphZone);
  Graph graph(scope.main_zone());
  CommonOperatorBuilder common(scope.main_zone());

  Node* start = graph.NewNode(common.Start(0));
  graph.SetStart(start);
  Node* n2 = graph.NewNode(&dummy_operator1, graph.start());
  Node* n3 = graph.NewNode(&dummy_operator1, graph.start());
  Node* n4 = graph.NewNode(&dummy_operator1, n2);
  Node* n5 = graph.NewNode(&dummy_operator1, n2);
  Node* n6 = graph.NewNode(&dummy_operator1, n3);
  Node* n7 = graph.NewNode(&dummy_operator1, n3);
  Node* n8 = graph.NewNode(&dummy_operator1, n5);
  Node* n9 = graph.NewNode(&dummy_operator1, n5);
  Node* n10 = graph.NewNode(&dummy_operator1, n9);
  Node* n11 = graph.NewNode(&dummy_operator1, n9);
  Node* end_dependencies[6] = {n4, n8, n10, n11, n6, n7};
  Node* end = graph.NewNode(&dummy_operator6, 6, end_dependencies);
  graph.SetEnd(end);

  SourcePositionTable table(&graph);
  NodeOriginTable table2(&graph);
  StdoutStream{} << AsJSON(graph, &table, &table2);
}

TEST(TestSourceIdAssigner) {
  Handle<SharedFunctionInfo> shared1;
  Handle<SharedFunctionInfo> shared2;

  SourceIdAssigner assigner(2);
  const int source_id1 = assigner.GetIdFor(shared1);
  const int source_id2 = assigner.GetIdFor(shared2);

  CHECK_EQ(source_id1, source_id2);
  CHECK_EQ(source_id1, assigner.GetIdAt(0));
  CHECK_EQ(source_id2, assigner.GetIdAt(1));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
