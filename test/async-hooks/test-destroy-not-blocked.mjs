// Flags: --expose_gc

import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import tick from '../common/tick.js';

import { createHook, AsyncResource } from 'async_hooks';
import process from 'process';

// Test priority of destroy hook relative to nextTick,... and
// verify a microtask is scheduled in case a lot items are queued

const resType = 'MyResource';
let activeId = -1;
createHook({
  init(id, type) {
    if (type === resType) {
      strictEqual(activeId, -1);
      activeId = id;
    }
  },
  destroy(id) {
    if (activeId === id) {
      activeId = -1;
    }
  }
}).enable();

function testNextTick() {
  strictEqual(activeId, -1);
  const res = new AsyncResource(resType);
  strictEqual(activeId, res.asyncId());
  res.emitDestroy();
  // nextTick has higher prio than emit destroy
  process.nextTick(mustCall(() =>
    strictEqual(activeId, res.asyncId()))
  );
}

function testQueueMicrotask() {
  strictEqual(activeId, -1);
  const res = new AsyncResource(resType);
  strictEqual(activeId, res.asyncId());
  res.emitDestroy();
  // queueMicrotask has higher prio than emit destroy
  queueMicrotask(mustCall(() =>
    strictEqual(activeId, res.asyncId()))
  );
}

function testImmediate() {
  strictEqual(activeId, -1);
  const res = new AsyncResource(resType);
  strictEqual(activeId, res.asyncId());
  res.emitDestroy();
  setImmediate(mustCall(() =>
    strictEqual(activeId, -1))
  );
}

function testPromise() {
  strictEqual(activeId, -1);
  const res = new AsyncResource(resType);
  strictEqual(activeId, res.asyncId());
  res.emitDestroy();
  // Promise has higher prio than emit destroy
  Promise.resolve().then(mustCall(() =>
    strictEqual(activeId, res.asyncId()))
  );
}

async function testAwait() {
  strictEqual(activeId, -1);
  const res = new AsyncResource(resType);
  strictEqual(activeId, res.asyncId());
  res.emitDestroy();

  for (let i = 0; i < 5000; i++) {
    await Promise.resolve();
  }
  globalThis.gc();
  await Promise.resolve();
  // Limit to trigger a microtask not yet reached
  strictEqual(activeId, res.asyncId());
  for (let i = 0; i < 5000; i++) {
    await Promise.resolve();
  }
  globalThis.gc();
  await Promise.resolve();
  strictEqual(activeId, -1);
}

testNextTick();
tick(2, testQueueMicrotask);
tick(4, testImmediate);
tick(6, testPromise);
tick(8, () => testAwait().then(mustCall()));
