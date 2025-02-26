'use strict';

require('../common');

const { test } = require('node:test');
const { once } = require('node:events');
const assert = require('node:assert');
const v8 = require('node:v8');
const { Worker } = require('node:worker_threads');

async function runWorker(options = {}) {
  const empiricalStackDepth = new Uint32Array(new SharedArrayBuffer(4));
  const worker = new Worker(
    `
    const { workerData: { empiricalStackDepth } } = require('worker_threads');
    function f() {
      empiricalStackDepth[0]++;
      f();
    }
    f();`,
    {
      eval: true,
      workerData: { empiricalStackDepth },
      ...options,
    },
  );

  const [error] = await once(worker, 'error');
  if (!options.skipErrorCheck) {
    const expectedErrorMessage = 'Expected a RangeError';
    const unexpectedErrorMessage = 'Unexpected error message';

    assert.strictEqual(error.constructor, RangeError, expectedErrorMessage);
    assert.match(
      error.message,
      /Maximum call stack size exceeded/,
      unexpectedErrorMessage,
    );
  }

  return empiricalStackDepth[0];
}

test('Worker threads stack size tests', async (t) => {
  await t.test('Workers are unaffected by --stack-size flag', async () => {
    v8.setFlagsFromString('--stack-size=500');
    const w1stack = await runWorker();
    v8.setFlagsFromString('--stack-size=1000');
    const w2stack = await runWorker();

    assert(
      Math.max(w1stack, w2stack) / Math.min(w1stack, w2stack) < 1.1,
      `w1stack = ${w1stack}, w2stack = ${w2stack} are too far apart`,
    );
  });

  await t.test('Workers respect resourceLimits.stackSizeMb', async () => {
    const w1stack = await runWorker({ resourceLimits: { stackSizeMb: 0.5 } });
    const w2stack = await runWorker({ resourceLimits: { stackSizeMb: 1.0 } });

    assert(
      w2stack > w1stack * 1.4,
      `w1stack = ${w1stack}, w2stack = ${w2stack} are too close`,
    );
  });

  await t.test('Low stack sizes result in error events', async () => {
    for (const stackSizeMb of [0.3, 0.5, 1]) {
      await runWorker({
        resourceLimits: { stackSizeMb },
        skipErrorCheck: true,
      });
    }
  });
});
