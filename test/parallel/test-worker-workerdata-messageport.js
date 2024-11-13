'use strict';

require('../common');
const assert = require('node:assert');

const {
  Worker, MessageChannel
} = require('node:worker_threads');

const channel = new MessageChannel();
const workerData = { message: channel.port1 };
const transferList = [channel.port1];
const meowScript = () => 'meow';

{
  // Should receive the transferList param.
  new Worker(`${meowScript}`, { eval: true, workerData, transferList });
}

{
  // Should work with more than one MessagePort.
  const channel1 = new MessageChannel();
  const channel2 = new MessageChannel();
  const workerData = { message: channel1.port1, message2: channel2.port1 };
  const transferList = [channel1.port1, channel2.port1];
  new Worker(`${meowScript}`, { eval: true, workerData, transferList });
}

{
  const uint8Array = new Uint8Array([ 1, 2, 3, 4 ]);
  assert.strictEqual(uint8Array.length, 4);
  new Worker(`
    const { parentPort, workerData } = require('worker_threads');
    parentPort.postMessage(workerData);
    `, {
    eval: true,
    workerData: uint8Array,
    transferList: [uint8Array.buffer]
  }).on(
    'message',
    (message) =>
      assert.deepStrictEqual(message, Uint8Array.of(1, 2, 3, 4))
  );
  assert.strictEqual(uint8Array.length, 0);
}

{
  // Should throw on non valid transferList input.
  const channel1 = new MessageChannel();
  const channel2 = new MessageChannel();
  const workerData = { message: channel1.port1, message2: channel2.port1 };
  assert.throws(() => new Worker(`${meowScript}`, {
    eval: true,
    workerData,
    transferList: []
  }), {
    constructor: DOMException,
    name: 'DataCloneError',
    code: 25,
    message: 'Object that needs transfer was found in message but not ' +
             'listed in transferList'
  });
}

{
  // Should not crash when MessagePort is transferred to another context.
  // https://github.com/nodejs/node/issues/49075
  const channel = new MessageChannel();
  new Worker(`
    const { runInContext, createContext } = require('node:vm')
    const { workerData } = require('worker_threads');
    const context = createContext(Object.create(null));
    context.messagePort = workerData.messagePort;
    runInContext(
      \`messagePort.postMessage("Meow")\`,
      context,
      { displayErrors: true }
    );
    `, {
    eval: true,
    workerData: { messagePort: channel.port2 },
    transferList: [channel.port2]
  });
  channel.port1.on(
    'message',
    (message) =>
      assert.strictEqual(message, 'Meow')
  );
}
