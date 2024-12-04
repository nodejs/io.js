'use strict';

// This test ensures that CryptoKey instances can be correctly
// sent to a Worker via postMessage.

const common = require('../common');
const assert = require('node:assert');
const { test } = require('node:test');
const { subtle } = globalThis.crypto;
const { once } = require('node:events');
const { Worker, parentPort } = require('node:worker_threads');


// Skip the test if crypto is not available
if (!common.hasCrypto) common.skip('missing crypto');

const keyData = Buffer.from(
  '000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f', 'hex'
);
const sig = '13691a79fb55a0417e4d6699a32f91ad29283fa2c1439865cc0632931f4f48dc';

async function doSig(key) {
  const signature = await subtle.sign({ name: 'HMAC' }, key, Buffer.from('some data'));
  assert.strictEqual(Buffer.from(signature).toString('hex'), sig);
}

// Main test logic
test('CryptoKey instances can be sent to a Worker via postMessage', async (t) => {
  await t.test('should process signature in Worker and main thread', async () => {
    if (process.env.HAS_STARTED_WORKER) {
      return parentPort.once('message', (key) => {
        assert.strictEqual(key.algorithm.name, 'HMAC');
        doSig(key).then(common.mustCall());
      });
    }

    // Set environment variable to trigger worker process
    process.env.HAS_STARTED_WORKER = 1;

    const worker = new Worker(__filename);

    // Wait for the worker to start
    await once(worker, 'online');

    // Create and send the CryptoKey to the worker
    const key = await subtle.importKey(
      'raw',
      keyData,
      { name: 'HMAC', hash: 'SHA-256' },
      true, ['sign', 'verify']
    );

    worker.postMessage(key);

    // Perform the signature in the main thread
    await doSig(key);
  });
});
