// Flags: --expose-internals
'use strict';
require('../common');

const { test } = require('node:test');
const { Worker } = require('node:worker_threads');
const { once } = require('node:events');
const { recordState } = require('../common/heap');

test('Worker threads heap snapshot validation', async (t) => {
  await t.test('should validate heap snapshot structure for MessagePort', async () => {
    const w = new Worker('setInterval(() => {}, 100)', { eval: true });

    // Wait for the Worker to be online
    await once(w, 'online');

    // Generate heap snapshot
    const stream = await w.getHeapSnapshot();
    const snapshot = recordState(stream);

    // Validate the snapshot
    snapshot.validateSnapshot(
      'Node / MessagePort',
      [
        {
          children: [
            { node_name: 'Node / MessagePortData', edge_name: 'data' },
          ],
        },
      ],
      { loose: true },
    );

    // Terminate the Worker
    await w.terminate();
  });
});
