// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-stress-incremental-marking

let {session, contextGroup, Protocol} = InspectorTest.start(
    'Tests weakness of edges from JSWeakRef and WeakCell.');

const kNodeName = 1;
const kNodeEdgeCount = 4;
const kNodeSize = 6;
const kEdgeType = 0;
const kEdgeName = 1;
const kEdgeTarget = 2;
const kEdgeSize = 3;

function EdgeName(snapshot, edgeIndex) {
  return snapshot['strings'][snapshot['edges'][edgeIndex + kEdgeName]];
}

function EdgeTarget(snapshot, edgeIndex) {
  return snapshot['edges'][edgeIndex + kEdgeTarget];
}

function EdgeType(snapshot, edgeIndex) {
  return snapshot['edges'][edgeIndex + kEdgeType];
}

function EdgeCount(snapshot, nodeIndex) {
  return snapshot['nodes'][nodeIndex + kNodeEdgeCount];
}

function NodeName(snapshot, nodeIndex) {
  return snapshot['strings'][snapshot['nodes'][nodeIndex + kNodeName]];
}

function NodeEdges(snapshot, nodeIndex) {
  let startEdgeIndex = 0;
  for (let i = 0; i < nodeIndex; i += kNodeSize) {
    startEdgeIndex += EdgeCount(snapshot, i);
  }
  let endEdgeIndex = startEdgeIndex + EdgeCount(snapshot, nodeIndex);
  let result = [];
  for (let i = startEdgeIndex; i < endEdgeIndex; ++i) {
    result.push(i * kEdgeSize);
  }
  return result;
}

function NodeByName(snapshot, name, start = 0) {
  let count = snapshot['nodes'].length / kNodeSize;
  for (let i = start; i < count; i++) {
    if (NodeName(snapshot, i * kNodeSize) == name) return i * kNodeSize;
  }
  InspectorTest.log(`Cannot find node ${name}`);
  return 0;
}

function FindEdge(snapshot, sourceIndex, targetName) {
  let edges = NodeEdges(snapshot, sourceIndex);
  for (let edge of edges) {
    let target = EdgeTarget(snapshot, edge);
    if (NodeName(snapshot, target) == targetName) return edge;
  }
  InspectorTest.log(
      `Cannot find edge between ${sourceIndex} and ${targetName}`);
  return 0;
}

function EdgeByName(snapshot, name, start = 0) {
  let count = snapshot.edges.length / kEdgeSize;
  for (let i = start; i < count; i++) {
    if (EdgeName(snapshot, i * kEdgeSize) == name) return i * kEdgeSize;
  }
  InspectorTest.log(`Cannot find edge ${name}`);
  return 0;
}

function EdgeTypeString(snapshot, edgeIndex) {
  return snapshot.snapshot.meta.edge_types[0][EdgeType(snapshot, edgeIndex)];
}

contextGroup.addScript(`
class Class1 {}
class Class2 {}
class Class3 {}
class Class4 {}
var class1Instance = new Class1();
var class2Instance = new Class2();
var class3Instance = new Class3();
var class4Instance = new Class4();
var weakRef = new WeakRef(class1Instance);
var finalizationRegistry = new FinalizationRegistry(()=>{});
finalizationRegistry.register(class2Instance, class3Instance, class4Instance);
//# sourceURL=test.js`);

Protocol.HeapProfiler.enable();

InspectorTest.runAsyncTestSuite([
  async function testHeapSnapshotJSWeakRefs() {
    let snapshot_string = '';
    function onChunk(message) {
      snapshot_string += message['params']['chunk'];
    }
    Protocol.HeapProfiler.onAddHeapSnapshotChunk(onChunk)
    await Protocol.HeapProfiler.takeHeapSnapshot({ reportProgress: false })
    let snapshot = JSON.parse(snapshot_string);

    // There should be a single edge named "weakRef", representing the global
    // variable of that name. It contains a weak ref to an instance of Class1.
    let weakRef = EdgeTarget(snapshot, EdgeByName(snapshot, "weakRef"));
    let edge = FindEdge(snapshot, weakRef, "Class1");
    let edgeType = EdgeTypeString(snapshot, edge);
    InspectorTest.log(`WeakRef target edge type: ${edgeType}`);

    // There should be a WeakCell representing the item registered in the
    // FinalizationRegistry. It retains the holdings strongly, but has weak
    // references to the target and unregister token.
    let weakCell = NodeByName(snapshot, "system / WeakCell");
    edge = FindEdge(snapshot, weakCell, "Class2");
    edgeType = EdgeTypeString(snapshot, edge);
    InspectorTest.log(`WeakCell target edge type: ${edgeType}`);
    edge = FindEdge(snapshot, weakCell, "Class3");
    edgeType = EdgeTypeString(snapshot, edge);
    InspectorTest.log(`WeakCell holdings edge type: ${edgeType}`);
    edge = FindEdge(snapshot, weakCell, "Class4");
    edgeType = EdgeTypeString(snapshot, edge);
    InspectorTest.log(`WeakCell unregister token edge type: ${edgeType}`);
  }
]);
