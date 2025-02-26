'use strict';

const common = require('../common');
const assert = require('node:assert');
const fs = require('node:fs').promises;

const { test } = require('node:test');
const { MessageChannel } = require('node:worker_threads');
const { once } = require('node:events');

// Test: Overriding the internal kTransfer method should not enable arbitrary code loading.
test('Overriding kTransfer method security test', async (t) => {
  await t.test('should throw an error for invalid deserializeInfo', async () => {
    const fh = await fs.open(__filename);
    assert.strictEqual(fh.constructor.name, 'FileHandle');

    const kTransfer = Object.getOwnPropertySymbols(Object.getPrototypeOf(fh))
      .find((symbol) => symbol.description === 'messaging_transfer_symbol');
    assert.strictEqual(typeof kTransfer, 'symbol');

    // Override the kTransfer method
    fh[kTransfer] = () => ({
      data: '✨',
      deserializeInfo: `${__filename}:NotARealClass`,
    });

    const { port1, port2 } = new MessageChannel();
    port1.postMessage(fh, [fh]);

    // Verify that no valid message is processed
    port2.on('message', common.mustNotCall());

    // Capture the 'messageerror' event and validate the error
    const [exception] = await once(port2, 'messageerror');
    assert.match(exception.message, /Missing internal module/, 'Unexpected error message');

    port2.close();
  });
});
