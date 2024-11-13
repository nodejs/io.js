'use strict';
const assert = require('assert');
const util = require('util');

let _buildEmbedderGraph;
function buildEmbedderGraph() {
  if (_buildEmbedderGraph) { return _buildEmbedderGraph(); }
  let internalBinding;
  try {
    internalBinding = require('internal/test/binding').internalBinding;
  } catch (e) {
    console.error('The test must be run with `--expose-internals`');
    throw e;
  }

  ({ buildEmbedderGraph: _buildEmbedderGraph } = internalBinding('heap_utils'));
  return _buildEmbedderGraph();
}

const { getHeapSnapshot } = require('v8');

function createJSHeapSnapshot(stream = getHeapSnapshot()) {
  stream.pause();
  const dump = JSON.parse(stream.read());
  const meta = dump.snapshot.meta;

  const nodes =
    readHeapInfo(dump.nodes, meta.node_fields, meta.node_types, dump.strings);
  const edges =
    readHeapInfo(dump.edges, meta.edge_fields, meta.edge_types, dump.strings);

  for (const node of nodes) {
    node.incomingEdges = [];
    node.outgoingEdges = [];
  }

  let fromNodeIndex = 0;
  let edgeIndex = 0;
  for (const { type, name_or_index, to_node } of edges) {
    while (edgeIndex === nodes[fromNodeIndex].edge_count) {
      edgeIndex = 0;
      fromNodeIndex++;
    }
    const toNode = nodes[to_node / meta.node_fields.length];
    const fromNode = nodes[fromNodeIndex];
    const edge = {
      type,
      to: toNode,
      from: fromNode,
      name: typeof name_or_index === 'string' ? name_or_index : null,
    };
    toNode.incomingEdges.push(edge);
    fromNode.outgoingEdges.push(edge);
    edgeIndex++;
  }

  for (const node of nodes) {
    assert.strictEqual(node.edge_count, node.outgoingEdges.length,
                       `${node.edge_count} !== ${node.outgoingEdges.length}`);
  }
  return nodes;
}

function readHeapInfo(raw, fields, types, strings) {
  const items = [];

  for (let i = 0; i < raw.length; i += fields.length) {
    const item = {};
    for (let j = 0; j < fields.length; j++) {
      const name = fields[j];
      let type = types[j];
      if (Array.isArray(type)) {
        item[name] = type[raw[i + j]];
      } else if (name === 'name_or_index') {  // type === 'string_or_number'
        if (item.type === 'element' || item.type === 'hidden')
          type = 'number';
        else
          type = 'string';
      }

      if (type === 'string') {
        item[name] = strings[raw[i + j]];
      } else if (type === 'number' || type === 'node') {
        item[name] = raw[i + j];
      }
    }
    items.push(item);
  }

  return items;
}

function inspectNode(snapshot) {
  return util.inspect(snapshot, { depth: 4 });
}

function isEdge(edge, { node_name, edge_name }) {
  if (edge_name !== undefined && edge.name !== edge_name) {
    return false;
  }
  // From our internal embedded graph
  if (edge.to.value) {
    if (edge.to.value.constructor.name !== node_name) {
      return false;
    }
  } else if (edge.to.name !== node_name) {
    return false;
  }
  return true;
}

class State {
  constructor(stream) {
    this.snapshot = createJSHeapSnapshot(stream);
    this.embedderGraph = buildEmbedderGraph();
  }

  // Validate the v8 heap snapshot
  validateSnapshot(rootName, expected, { loose = false } = {}) {
    const rootNodes = this.snapshot.filter(
      (node) => node.name === rootName && node.type !== 'string');
    if (loose) {
      assert(rootNodes.length >= expected.length,
             `Expect to find at least ${expected.length} '${rootName}', ` +
             `found ${rootNodes.length}`);
    } else {
      assert.strictEqual(
        rootNodes.length, expected.length,
        `Expect to find ${expected.length} '${rootName}', ` +
        `found ${rootNodes.length}`);
    }

    for (const expectation of expected) {
      if (expectation.children) {
        for (const expectedEdge of expectation.children) {
          const check = typeof expectedEdge === 'function' ? expectedEdge :
            (edge) => (isEdge(edge, expectedEdge));
          const hasChild = rootNodes.some(
            (node) => node.outgoingEdges.some(check),
          );
          // Don't use assert with a custom message here. Otherwise the
          // inspection in the message is done eagerly and wastes a lot of CPU
          // time.
          if (!hasChild) {
            throw new Error(
              'expected to find child ' +
              `${util.inspect(expectedEdge)} in ${inspectNode(rootNodes)}`);
          }
        }
      }

      if (expectation.detachedness !== undefined) {
        const matchedNodes = rootNodes.filter(
          (node) => node.detachedness === expectation.detachedness);
        if (loose) {
          assert(matchedNodes.length >= rootNodes.length,
                 `Expect to find at least ${rootNodes.length} with ` +
                `detachedness ${expectation.detachedness}, ` +
                `found ${matchedNodes.length}`);
        } else {
          assert.strictEqual(
            matchedNodes.length, rootNodes.length,
            `Expect to find ${rootNodes.length} with detachedness ` +
            `${expectation.detachedness},  found ${matchedNodes.length}`);
        }
      }
    }
  }

  // Validate our internal embedded graph representation
  validateGraph(rootName, expected, { loose = false } = {}) {
    const rootNodes = this.embedderGraph.filter(
      (node) => node.name === rootName,
    );
    if (loose) {
      assert(rootNodes.length >= expected.length,
             `Expect to find at least ${expected.length} '${rootName}', ` +
             `found ${rootNodes.length}`);
    } else {
      assert.strictEqual(
        rootNodes.length, expected.length,
        `Expect to find ${expected.length} '${rootName}', ` +
        `found ${rootNodes.length}`);
    }
    for (const expectation of expected) {
      if (expectation.children) {
        for (const expectedEdge of expectation.children) {
          const check = typeof expectedEdge === 'function' ? expectedEdge :
            (edge) => (isEdge(edge, expectedEdge));
          // Don't use assert with a custom message here. Otherwise the
          // inspection in the message is done eagerly and wastes a lot of CPU
          // time.
          const hasChild = rootNodes.some(
            (node) => node.edges.some(check),
          );
          if (!hasChild) {
            throw new Error(
              'expected to find child ' +
              `${util.inspect(expectedEdge)} in ${inspectNode(rootNodes)}`);
          }
        }
      }
    }
  }

  validateSnapshotNodes(rootName, expected, { loose = false } = {}) {
    this.validateSnapshot(rootName, expected, { loose });
    this.validateGraph(rootName, expected, { loose });
  }
}

function recordState(stream = undefined) {
  return new State(stream);
}

function validateSnapshotNodes(...args) {
  return recordState().validateSnapshotNodes(...args);
}

/**
 * A alternative heap snapshot validator that can be used to verify cppgc-managed nodes.
 * Modified from
 * https://chromium.googlesource.com/v8/v8/+/b00e995fb212737802810384ba2b868d0d92f7e5/test/unittests/heap/cppgc-js/unified-heap-snapshot-unittest.cc#134
 * @param {string} rootName Name of the root node. Typically a class name used to filter all native nodes with
 *                          this name. For cppgc-managed objects, this is typically the name configured by
 *                          SET_CPPGC_NAME() prefixed with an additional "Node /" prefix e.g.
 *                          "Node / ContextifyScript"
 * @param {[{
 *   node_name?: string,
 *   edge_name?: string,
 *   node_type?: string,
 *   edge_type?: string,
 * }]} retainingPath The retaining path specification to search from the root nodes.
 * @returns {[object]} All the leaf nodes matching the retaining path specification. If none can be found,
 *                     logs the nodes found in the last matching step of the path (if any), and throws an
 *                     assertion error.
 */
function findByRetainingPath(rootName, retainingPath) {
  const nodes = createJSHeapSnapshot();
  let haystack = nodes.filter((n) => n.name === rootName && n.type !== 'string');

  for (let i = 0; i < retainingPath.length; ++i) {
    const expected = retainingPath[i];
    const newHaystack = [];

    for (const parent of haystack) {
      for (let j = 0; j < parent.outgoingEdges.length; j++) {
        const edge = parent.outgoingEdges[j];
        // The strings are represented as { type: 'string', name: '<string content>' } in the snapshot.
        // Ignore them or we'll poke into strings that are just referenced as names of real nodes,
        // unless the caller is specifically looking for string nodes via `node_type`.
        let match = (edge.to.type !== 'string');
        if (expected.node_type) {
          match = (edge.to.type === expected.node_type);
        }
        if (expected.node_name && edge.to.name !== expected.node_name) {
          match = false;
        }
        if (expected.edge_name && edge.name !== expected.edge_name) {
          match = false;
        }
        if (expected.edge_type && edge.type !== expected.type) {
          match = false;
        }
        if (match) {
          newHaystack.push(edge.to);
        }
      }
    }

    if (newHaystack.length === 0) {
      const format = (val) => util.inspect(val, { breakLength: 128, depth: 3 });
      console.error('#');
      console.error('# Retaining path to search for:');
      for (let j = 0; j < retainingPath.length; ++j) {
        console.error(`# - '${format(retainingPath[j])}'${i === j ? '\t<--- not found' : ''}`);
      }
      console.error('#\n');
      console.error('# Nodes found in the last step include:');
      for (let j = 0; j < haystack.length; ++j) {
        console.error(`# - '${format(haystack[j])}`);
      }

      assert.fail(`Could not find target edge ${format(expected)} in the heap snapshot.`);
    }

    haystack = newHaystack;
  }

  return haystack;
}

function getHeapSnapshotOptionTests() {
  const fixtures = require('../common/fixtures');
  const cases = [
    {
      options: { exposeInternals: true },
      expected: [{
        children: [
          // We don't have anything special to test here yet
          // because we don't use cppgc or embedder heap tracer.
          { edge_name: 'nonNumeric', node_name: 'test' },
        ],
      }],
    },
    {
      options: { exposeNumericValues: true },
      expected: [{
        children: [
          { edge_name: 'numeric', node_name: 'smi number' },
        ],
      }],
    },
  ];
  return {
    fixtures: fixtures.path('klass-with-fields.js'),
    check(snapshot, expected) {
      snapshot.validateSnapshot('Klass', expected, { loose: true });
    },
    cases,
  };
}

module.exports = {
  recordState,
  validateSnapshotNodes,
  findByRetainingPath,
  getHeapSnapshotOptionTests,
};
