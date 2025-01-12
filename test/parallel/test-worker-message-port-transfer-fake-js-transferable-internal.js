'use strict';

const common = require('../common');
const fs = require('node:fs').promises;
const assert = require('node:assert');

const { test } = require('node:test');
const { MessageChannel } = require('node:worker_threads');
const { once } = require('node:events');

// Test: Overriding kTransfer method should not enable loading arbitrary code from Node.js core modules.
test('Security test for overriding kTransfer method', async (t) => {
  await t.test(
    'should throw an error for invalid deserializeInfo from core module',
    async () => {
      const fh = await fs.open(__filename);
      assert.strictEqual(fh.constructor.name, 'FileHandle');

      const kTransfer = Object.getOwnPropertySymbols(
        Object.getPrototypeOf(fh),
      ).find((symbol) => symbol.description === 'messaging_transfer_symbol');
      assert.strictEqual(typeof kTransfer, 'symbol');

      // Override the kTransfer method
      fh[kTransfer] = () => ({
        data: '✨',
        deserializeInfo: 'net:Socket', // Attempt to load an internal module
      });

      const { port1, port2 } = new MessageChannel();
      port1.postMessage(fh, [fh]);

      // Verify that no valid message is processed
      port2.on('message', common.mustNotCall());

      // Capture the 'messageerror' event and validate the error
      const [exception] = await once(port2, 'messageerror');
      const errorMessage =
        'Unexpected error message for invalid deserializeInfo';

      assert.strictEqual(
        exception.message,
        'Unknown deserialize spec net:Socket',
        errorMessage,
      );

      port2.close();
    },
  );
});
