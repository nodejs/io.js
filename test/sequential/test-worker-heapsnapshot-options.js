// Flags: --expose-internals
'use strict';

require('../common');

const { recordState, getHeapSnapshotOptionTests } = require('../common/heap');
const { Worker } = require('node:worker_threads');
const { once } = require('node:events');
const { test } = require('node:test');

test('should handle heap snapshot options correctly in Worker threads', async () => {
  const tests = getHeapSnapshotOptionTests();
  const w = new Worker(tests.fixtures);

  await once(w, 'message');

  for (const { options, expected } of tests.cases) {
    const stream = await w.getHeapSnapshot(options);
    const snapshot = recordState(stream);
    tests.check(snapshot, expected);
  }

  await w.terminate();
});
