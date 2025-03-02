'use strict';

require('../common');

const { test } = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs').promises;
const vm = require('node:vm');
const { MessageChannel, moveMessagePortToContext } = require('node:worker_threads');
const { once } = require('node:events');

test('FileHandle transfer behavior', async (t) => {
  await t.test('should throw when posting a FileHandle without transferring', async () => {
    const fh = await fs.open(__filename);
    const { port1 } = new MessageChannel();

    assert.throws(() => {
      port1.postMessage(fh);
    }, {
      constructor: DOMException,
      name: 'DataCloneError',
      code: 25,
    });

    await fh.close();
  });

  await t.test('should transfer a FileHandle successfully', async () => {
    const fh = await fs.open(__filename);
    const { port1, port2 } = new MessageChannel();

    assert.notStrictEqual(fh.fd, -1);
    port1.postMessage(fh, [fh]);
    assert.strictEqual(fh.fd, -1);

    const [fh2] = await once(port2, 'message');
    assert.strictEqual(Object.getPrototypeOf(fh2), Object.getPrototypeOf(fh));

    assert.deepStrictEqual(await fh2.readFile(), await fs.readFile(__filename));
    await fh2.close();

    await assert.rejects(() => fh.readFile(), { code: 'EBADF' });
  });

  await t.test('should not crash if the message is never read', async () => {
    const fh = await fs.open(__filename);
    const { port1 } = new MessageChannel();

    assert.notStrictEqual(fh.fd, -1);
    port1.postMessage(fh, [fh]);
    assert.strictEqual(fh.fd, -1);
  });

  await t.test('should discard message with context mismatch', async () => {
    const fh = await fs.open(__filename);
    const { port1, port2 } = new MessageChannel();

    const ctx = vm.createContext();
    const port2moved = moveMessagePortToContext(port2, ctx);
    port2moved.onmessage = (msgEvent) => {
      assert.strictEqual(msgEvent.data, 'second message');
      port1.close();
    };
    port2moved.onmessageerror = (event) => {
      assert.strictEqual(
        event.data.code,
        'ERR_MESSAGE_TARGET_CONTEXT_UNAVAILABLE'
      );
    };
    port2moved.start();

    assert.notStrictEqual(fh.fd, -1);
    port1.postMessage(fh, [fh]);
    assert.strictEqual(fh.fd, -1);

    port1.postMessage('second message');
  });

  await t.test('should not transfer FileHandle with read in progress', async () => {
    const fh = await fs.open(__filename);
    const { port1 } = new MessageChannel();

    const readPromise = fh.readFile();
    assert.throws(() => {
      port1.postMessage(fh, [fh]);
    }, {
      message: 'Cannot transfer FileHandle while in use',
      name: 'DataCloneError',
    });

    assert.deepStrictEqual(await readPromise, await fs.readFile(__filename));
  });

  await t.test('should not transfer FileHandle with close in progress', async () => {
    const fh = await fs.open(__filename);
    const { port1 } = new MessageChannel();

    const closePromise = fh.close();
    assert.throws(() => {
      port1.postMessage(fh, [fh]);
    }, {
      message: 'Cannot transfer FileHandle while in use',
      name: 'DataCloneError',
    });
    await closePromise;
  });
});
