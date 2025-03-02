// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-stress-incremental-marking

let {session, contextGroup, Protocol} = InspectorTest.start(
    'Tests edge labels of objects retained by DevTools.');

const kNodeName = 1;
const kNodeEdgeCount = 4;
const kNodeSize = 6;
const kEdgeName = 1;
const kEdgeTarget = 2;
const kEdgeSize = 3;

function EdgeName(snapshot, edgeIndex) {
  return snapshot['strings'][snapshot['edges'][edgeIndex + kEdgeName]];
}

function EdgeTarget(snapshot, edgeIndex) {
  return snapshot['edges'][edgeIndex + kEdgeTarget];
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

function NodeByName(snapshot, name) {
  let count = snapshot['nodes'].length / kNodeSize;
  for (let i = 0; i < count; i++) {
    if (NodeName(snapshot, i * kNodeSize) == name) return i * kNodeSize;
  }
  InspectorTest.log(`Cannot node ${name}`);
  return 0;
}

function FindEdge(snapshot, sourceName, targetName) {
  let sourceIndex = NodeByName(snapshot, sourceName);
  let targetIndex = NodeByName(snapshot, targetName);
  let edges = NodeEdges(snapshot, sourceIndex);
  for (let edge of edges) {
    if (EdgeTarget(snapshot, edge) == targetIndex) return edge;
  }
  InspectorTest.log(`Cannot find edge between ${sourceName} and ${targetName}`);
  return 0;
}

function GlobalHandleEdgeName(snapshot, targetName) {
  let edge = FindEdge(snapshot, '(Global handles)', targetName);
  let edgeName = EdgeName(snapshot, edge);
  // Make the test more robust by skipping the edge index prefix and
  // a single space.
  return edgeName.substring(edgeName.indexOf('/') + 2);
}

contextGroup.addScript(`
class MyClass1 {};
class MyClass2 {};
//# sourceURL=test.js`);

Protocol.Debugger.enable();
Protocol.HeapProfiler.enable();

InspectorTest.runAsyncTestSuite([
  async function testConsoleRetainingPath() {
    let snapshot_string = '';
    function onChunk(message) {
      snapshot_string += message['params']['chunk'];
    }
    Protocol.HeapProfiler.onAddHeapSnapshotChunk(onChunk)
    await Protocol.Runtime.evaluate({ expression: 'new MyClass1();' });
    await Protocol.Runtime.evaluate(
        { expression: 'console.log(new MyClass2());' });
    await Protocol.HeapProfiler.takeHeapSnapshot({ reportProgress: false })
    let snapshot = JSON.parse(snapshot_string);
    let edge1 = GlobalHandleEdgeName(snapshot, 'MyClass1');
    let edge2 = GlobalHandleEdgeName(snapshot, 'MyClass2');
    InspectorTest.log(`Edge from (Global handles) to MyClass1: ${edge1}`);
    InspectorTest.log(`Edge from (Global handles) to MyClass2: ${edge2}`);
  }
]);
