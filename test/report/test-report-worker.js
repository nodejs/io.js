'use strict';

require('../common');

const helper = require('../common/report');
const assert = require('node:assert');
const { describe, it } = require('node:test');
const { Worker } = require('node:worker_threads');
const { once } = require('node:events');

describe('Worker threads', () => {
  it('should include basic information about Worker threads', async () => {
    const w = new Worker(`
      const { parentPort } = require('worker_threads');
      parentPort.once('message', () => {
        /* Wait for message to stop the Worker */
      });
    `, { eval: true });

    await once(w, 'online');

    const report = process.report.getReport();
    helper.validateContent(report);
    const workerLengthMessage = 'Report should include one Worker';
    const threadIdMessage = 'Thread ID should match the Worker thread ID';

    assert.strictEqual(report.workers.length, 1, workerLengthMessage);
    helper.validateContent(report.workers[0]);
    assert.strictEqual(report.workers[0].header.threadId, w.threadId, threadIdMessage);


    w.postMessage({});


    await once(w, 'exit');
  });

  it('should generate report when Workers are busy in JS land', async () => {
    const w = new Worker('while (true);', { eval: true });

    await once(w, 'online');

    const report = process.report.getReport();
    helper.validateContent(report);
    const workerLengthMessage = 'Report should include one Worker';

    assert.strictEqual(report.workers.length, 1, workerLengthMessage);
    helper.validateContent(report.workers[0]);

    await w.terminate();
  });
});
