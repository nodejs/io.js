// Flags: --expose-internals
'use strict';

// This tests that Environment is tracked in heap snapshots.
// Tests for BaseObject and cppgc-managed objects are done in other
// test-heapdump-*.js files.

require('../common');
const { validateSnapshotNodes } = require('../common/heap');

validateSnapshotNodes('Node / Environment', [{
  children: [
    { node_name: 'Node / CleanupQueue', edge_name: 'cleanup_queue' },
    { node_name: 'Node / IsolateData', edge_name: 'isolate_data' },
    { node_name: 'Node / PrincipalRealm', edge_name: 'principal_realm' },
  ],
}]);

validateSnapshotNodes('Node / PrincipalRealm', [{
  children: [
    { node_name: 'process', edge_name: 'process_object' },
    { node_name: 'Node / BaseObjectList', edge_name: 'base_object_list' },
  ],
}]);
