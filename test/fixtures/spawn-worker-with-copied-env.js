'use strict';

// This test is meant to be spawned with NODE_OPTIONS=--title=foo
const assert = require('assert');
if (process.platform !== 'sunos' && process.platform !== 'os400') {  // --title is unsupported on SmartOS and IBM i.
  assert.strictEqual(process.title, 'foo');
}

// Spawns a worker that may copy NODE_OPTIONS if it's set by the parent.
const { Worker } = require('worker_threads');
new Worker(`require('assert').strictEqual(process.env.TEST_VAR, 'bar')`, {
  env: {
    ...process.env,
    TEST_VAR: 'bar',
  },
  eval: true,
});
