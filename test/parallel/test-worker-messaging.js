'use strict';

const common = require('../common');
const Countdown = require('../common/countdown');
const {
  parentPort,
  postMessageToThread,
  threadId,
  workerData,
  Worker,
} = require('node:worker_threads');
const { strictEqual, deepStrictEqual } = require('node:assert');
const { once } = require('node:events');

// Spawn threads on three levels: 1 main thread, two children, four grand childrens. 7 threads total, max id = 6
const MAX_LEVEL = 2;
const MAX_THREAD = 6;

// This is to allow the test to run in --worker mode
const mainThread = workerData?.mainThread ?? threadId;
const level = workerData?.level ?? 0;

const channel = new BroadcastChannel('nodejs:test-worker-connection');
let completed;

if (level === 0) {
  completed = new Countdown(MAX_THREAD + 1, () => {
    channel.postMessage('exit');
    channel.close();
  });
}

async function createChildren() {
  const worker = new Worker(__filename, { workerData: { mainThread, level: level + 1 } });
  await once(worker, 'message');
}

async function ping() {
  let target;
  do {
    target = mainThread + Math.floor(Math.random() * MAX_THREAD);
  } while (target === threadId);

  const { port1, port2 } = new MessageChannel();
  await postMessageToThread(target, { level, port: port2 }, [port2]);

  port1.on('message', common.mustCall(function(message) {
    deepStrictEqual(message, { message: 'pong', source: target, destination: threadId });
    port1.close();

    if (level === 0) {
      completed.dec();
    } else {
      channel.postMessage('end');
    }
  }));

  port1.postMessage({ message: 'ping', source: threadId, destination: target });
}

// Do not use mustCall here as the thread might not receive any connection request
process.on('workerMessage', ({ port, level }, source) => {
  // Let's verify the source hierarchy
  // Given we do depth first, the level is 1 for thread 1 and 4, 2 for other threads
  if (source !== mainThread) {
    const currentThread = source - mainThread;
    strictEqual(level, (currentThread === 1 || currentThread === 4) ? 1 : 2);
  } else {
    strictEqual(level, 0);
  }

  // Verify communication
  port.on('message', common.mustCall(function(message) {
    deepStrictEqual(message, { message: 'ping', source, destination: threadId });
    port.postMessage({ message: 'pong', source: threadId, destination: source });
    port.close();
  }));
});

async function test() {
  if (level < MAX_LEVEL) {
    await createChildren();
    await createChildren();
  }

  channel.onmessage = function(message) {
    switch (message.data) {
      case 'start':
        ping();
        break;
      case 'end':
        if (level === 0) {
          completed.dec();
        }
        break;
      case 'exit':
        channel.close();
        break;
    }
  };

  if (level > 0) {
    const currentThread = threadId - mainThread;
    strictEqual(level, (currentThread === 1 || currentThread === 4) ? 1 : 2);
    parentPort.postMessage({ type: 'ready', threadId });
  } else {
    channel.postMessage('start');
    ping();
  }
}

test();
