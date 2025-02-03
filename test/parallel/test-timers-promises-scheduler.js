'use strict';

const common = require('../common');

const { scheduler } = require('timers/promises');
const { setTimeout } = require('timers');
const {
  strictEqual,
  rejects,
  throws,
} = require('assert');

async function testYield() {
  await scheduler.yield();
  process.emit('foo');
}
testYield().then(common.mustCall());
queueMicrotask(common.mustCall(() => {
  process.addListener('foo', common.mustCall());
}));

async function testWait() {
  let value = 0;
  setTimeout(() => value++, 10);
  await scheduler.wait(15);
  strictEqual(value, 1);
}

testWait().then(common.mustCall());

async function testCancelableWait1() {
  const ac = new AbortController();
  const wait = scheduler.wait(1e6, { signal: ac.signal });
  ac.abort();
  await rejects(wait, {
    code: 'ABORT_ERR',
    message: 'The operation was aborted',
  });
}

testCancelableWait1().then(common.mustCall());

async function testCancelableWait2() {
  const wait = scheduler.wait(10000, { signal: AbortSignal.abort() });
  await rejects(wait, {
    code: 'ABORT_ERR',
    message: 'The operation was aborted',
  });
}

testCancelableWait2().then(common.mustCall());

throws(() => new scheduler.constructor(), {
  code: 'ERR_ILLEGAL_CONSTRUCTOR',
});
