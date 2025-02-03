'use strict';
process.env.NODE_TEST_KNOWN_GLOBALS = 0;
const common = require('../common');

const assert = require('node:assert');
const { it, describe } = require('node:test');
const nodeTimersPromises = require('node:timers/promises');

describe('Mock Timers Scheduler Test Suite', () => {
  it('should advance in time and trigger timers when calling the .tick function', (t) => {
    t.mock.timers.enable({ apis: ['scheduler.wait'] });

    const now = Date.now();
    const durationAtMost = 100;

    const p = nodeTimersPromises.scheduler.wait(4000);
    t.mock.timers.tick(4000);

    return p.then(common.mustCall((result) => {
      assert.strictEqual(result, undefined);
      assert.ok(
        Date.now() - now < durationAtMost,
        `time should be advanced less than the ${durationAtMost}ms`
      );
    }));
  });

  it('should advance in time and trigger timers when calling the .tick function multiple times', async (t) => {
    t.mock.timers.enable({ apis: ['scheduler.wait'] });

    const fn = t.mock.fn();

    nodeTimersPromises.scheduler.wait(9999).then(fn);

    t.mock.timers.tick(8999);
    assert.strictEqual(fn.mock.callCount(), 0);
    t.mock.timers.tick(500);

    await nodeTimersPromises.setImmediate();

    assert.strictEqual(fn.mock.callCount(), 0);
    t.mock.timers.tick(500);

    await nodeTimersPromises.setImmediate();
    assert.strictEqual(fn.mock.callCount(), 1);
  });

  it('should work with the same params as the original timers/promises/scheduler.wait', async (t) => {
    t.mock.timers.enable({ apis: ['scheduler.wait'] });
    const controller = new AbortController();
    const p = nodeTimersPromises.scheduler.wait(2000, {
      ref: true,
      signal: controller.signal,
    });

    t.mock.timers.tick(1000);
    t.mock.timers.tick(500);
    t.mock.timers.tick(500);
    t.mock.timers.tick(500);

    const result = await p;
    assert.strictEqual(result, undefined);
  });

  it('should abort operation if timers/promises/scheduler.wait received an aborted signal', async (t) => {
    t.mock.timers.enable({ apis: ['scheduler.wait'] });
    const controller = new AbortController();
    const p = nodeTimersPromises.scheduler.wait(2000, {
      ref: true,
      signal: controller.signal,
    });

    t.mock.timers.tick(1000);
    controller.abort();
    t.mock.timers.tick(500);
    t.mock.timers.tick(500);
    t.mock.timers.tick(500);

    await assert.rejects(() => p, {
      name: 'AbortError',
    });
  });
  it('should abort operation even if the .tick was not called', async (t) => {
    t.mock.timers.enable({ apis: ['scheduler.wait'] });
    const controller = new AbortController();
    const p = nodeTimersPromises.scheduler.wait(2000, {
      ref: true,
      signal: controller.signal,
    });

    controller.abort();

    await assert.rejects(() => p, {
      name: 'AbortError',
    });
  });

  it('should abort operation when .abort is called before calling setInterval', async (t) => {
    t.mock.timers.enable({ apis: ['scheduler.wait'] });
    const controller = new AbortController();
    controller.abort();
    const p = nodeTimersPromises.scheduler.wait(2000, {
      ref: true,
      signal: controller.signal,
    });

    await assert.rejects(() => p, {
      name: 'AbortError',
    });
  });

  it('should reject given an an invalid signal instance', async (t) => {
    t.mock.timers.enable({ apis: ['scheduler.wait'] });
    const p = nodeTimersPromises.scheduler.wait(2000, {
      ref: true,
      signal: {},
    });

    await assert.rejects(() => p, {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE',
    });
  });
});
