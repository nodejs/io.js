// Flags: --expose-internals
'use strict';
process.env.NODE_TEST_KNOWN_GLOBALS = 0;
const common = require('../common');

const assert = require('node:assert');
const { it, mock, describe } = require('node:test');
const nodeTimers = require('node:timers');
const nodeTimersPromises = require('node:timers/promises');
const { TIMEOUT_MAX } = require('internal/timers');

describe('Mock Timers Test Suite', () => {
  describe('MockTimers API', () => {
    it('should throw an error if trying to enable a timer that is not supported', (t) => {
      assert.throws(() => {
        t.mock.timers.enable({ apis: ['DOES_NOT_EXIST'] });
      }, {
        code: 'ERR_INVALID_ARG_VALUE',
      });
    });

    it('should throw an error if data type of trying to enable a timer is not string', (t) => {
      assert.throws(() => {
        t.mock.timers.enable({ apis: [1] });
      }, {
        code: 'ERR_INVALID_ARG_TYPE',
      });
    });

    it('should throw an error if trying to enable a timer twice', (t) => {
      t.mock.timers.enable();
      assert.throws(() => {
        t.mock.timers.enable();
      }, {
        code: 'ERR_INVALID_STATE',
      });
    });

    it('should not throw if calling reset without enabling timers', (t) => {
      t.mock.timers.reset();
    });

    it('should throw an error if calling tick without enabling timers', (t) => {
      assert.throws(() => {
        t.mock.timers.tick();
      }, {
        code: 'ERR_INVALID_STATE',
      });
    });

    it('should throw an error if calling tick with a negative number', (t) => {
      t.mock.timers.enable();
      assert.throws(() => {
        t.mock.timers.tick(-1);
      }, {
        code: 'ERR_INVALID_ARG_VALUE',
      });
    });

    it('should check that propertyDescriptor gets back after resetting timers', (t) => {
      const getDescriptor = (ctx, fn) => Object.getOwnPropertyDescriptor(ctx, fn);
      const getCurrentTimersDescriptors = () => {
        const timers = [
          'setTimeout',
          'clearTimeout',
          'setInterval',
          'clearInterval',
          'setImmediate',
          'clearImmediate',
        ];

        const globalTimersDescriptors = timers.map((fn) => getDescriptor(globalThis, fn));
        const nodeTimersDescriptors = timers.map((fn) => getDescriptor(nodeTimers, fn));
        const nodeTimersPromisesDescriptors = timers
          .filter((fn) => !fn.includes('clear'))
          .map((fn) => getDescriptor(nodeTimersPromises, fn));

        return {
          global: globalTimersDescriptors,
          nodeTimers: nodeTimersDescriptors,
          nodeTimersPromises: nodeTimersPromisesDescriptors,
        };
      };

      const originalDescriptors = getCurrentTimersDescriptors();

      t.mock.timers.enable();
      const during = getCurrentTimersDescriptors();
      t.mock.timers.reset();
      const after = getCurrentTimersDescriptors();

      for (const env in originalDescriptors) {
        for (const prop in originalDescriptors[env]) {
          const originalDescriptor = originalDescriptors[env][prop];
          const afterDescriptor = after[env][prop];

          assert.deepStrictEqual(
            originalDescriptor,
            afterDescriptor,
          );

          assert.notDeepStrictEqual(
            originalDescriptor,
            during[env][prop],
          );

          assert.notDeepStrictEqual(
            during[env][prop],
            after[env][prop],
          );

        }
      }
    });

    it('should reset all timers when calling .reset function', (t) => {
      t.mock.timers.enable();
      const fn = t.mock.fn();
      globalThis.setTimeout(fn, 1000);
      t.mock.timers.reset();
      assert.deepStrictEqual(Date.now, globalThis.Date.now);
      assert.throws(() => {
        t.mock.timers.tick(1000);
      }, {
        code: 'ERR_INVALID_STATE',
      });

      assert.strictEqual(fn.mock.callCount(), 0);
    });

    it('should reset all timers when calling Symbol.dispose', (t) => {
      t.mock.timers.enable();
      const fn = t.mock.fn();
      globalThis.setTimeout(fn, 1000);
      // TODO(benjamingr) refactor to `using`
      t.mock.timers[Symbol.dispose]();
      assert.throws(() => {
        t.mock.timers.tick(1000);
      }, {
        code: 'ERR_INVALID_STATE',
      });

      assert.strictEqual(fn.mock.callCount(), 0);
    });

    it('should execute in order if timeout is the same', (t) => {
      t.mock.timers.enable();
      const order = [];
      const fn1 = t.mock.fn(() => order.push('f1'));
      const fn2 = t.mock.fn(() => order.push('f2'));
      globalThis.setTimeout(fn1, 1000);
      globalThis.setTimeout(fn2, 1000);
      t.mock.timers.tick(1000);
      assert.strictEqual(fn1.mock.callCount(), 1);
      assert.strictEqual(fn2.mock.callCount(), 1);
      assert.deepStrictEqual(order, ['f1', 'f2']);
    });

    describe('runAll Suite', () => {
      it('should throw an error if calling runAll without enabling timers', (t) => {
        assert.throws(() => {
          t.mock.timers.runAll();
        }, {
          code: 'ERR_INVALID_STATE',
        });
      });

      it('should trigger all timers when calling .runAll function', async (t) => {
        const timeoutFn = t.mock.fn();
        const intervalFn = t.mock.fn();

        t.mock.timers.enable();
        globalThis.setTimeout(timeoutFn, 1111);
        const id = globalThis.setInterval(intervalFn, 9999);
        t.mock.timers.runAll();

        globalThis.clearInterval(id);
        assert.strictEqual(timeoutFn.mock.callCount(), 1);
        assert.strictEqual(intervalFn.mock.callCount(), 1);
      });

      it('should increase the epoch as the tick run for runAll', async (t) => {
        const timeoutFn = t.mock.fn();
        const intervalFn = t.mock.fn();

        t.mock.timers.enable();
        globalThis.setTimeout(timeoutFn, 1111);
        const id = globalThis.setInterval(intervalFn, 9999);
        t.mock.timers.runAll();

        globalThis.clearInterval(id);
        assert.strictEqual(timeoutFn.mock.callCount(), 1);
        assert.strictEqual(intervalFn.mock.callCount(), 1);
        assert.strictEqual(Date.now(), 9999);
      });

      it('should not error if there are not timers to run', (t) => {
        t.mock.timers.enable();
        t.mock.timers.runAll();
        // Should not throw
      });
    });
  });

  describe('globals/timers', () => {
    describe('setTimeout Suite', () => {
      it('should advance in time and trigger timers when calling the .tick function', (t) => {
        mock.timers.enable({ apis: ['setTimeout'] });

        const fn = mock.fn();

        globalThis.setTimeout(fn, 4000);

        mock.timers.tick(4000);
        assert.strictEqual(fn.mock.callCount(), 1);
        mock.timers.reset();
      });

      it('should advance in time and trigger timers when calling the .tick function multiple times', (t) => {
        t.mock.timers.enable({ apis: ['setTimeout'] });
        const fn = t.mock.fn();

        globalThis.setTimeout(fn, 2000);

        t.mock.timers.tick(1000);
        assert.strictEqual(fn.mock.callCount(), 0);
        t.mock.timers.tick(500);
        assert.strictEqual(fn.mock.callCount(), 0);
        t.mock.timers.tick(500);
        assert.strictEqual(fn.mock.callCount(), 1);
      });

      it('should work with the same params as the original setTimeout', (t) => {
        t.mock.timers.enable({ apis: ['setTimeout'] });
        const fn = t.mock.fn();
        const args = ['a', 'b', 'c'];
        globalThis.setTimeout(fn, 2000, ...args);

        t.mock.timers.tick(1000);
        t.mock.timers.tick(500);
        t.mock.timers.tick(500);

        assert.strictEqual(fn.mock.callCount(), 1);
        assert.deepStrictEqual(fn.mock.calls[0].arguments, args);
      });

      it('should keep setTimeout working if timers are disabled', (t, done) => {
        const now = Date.now();
        const timeout = 2;
        const expected = () => now - timeout;
        globalThis.setTimeout(common.mustCall(() => {
          assert.strictEqual(now - timeout, expected());
          done();
        }), timeout);
      });

      it('should change timeout to 1ms when it is > TIMEOUT_MAX', (t) => {
        t.mock.timers.enable({ apis: ['setTimeout'] });
        const fn = t.mock.fn();
        globalThis.setTimeout(fn, TIMEOUT_MAX + 1);
        t.mock.timers.tick(1);
        assert.strictEqual(fn.mock.callCount(), 1);
      });

      it('should change the delay to one if timeout < 0', (t) => {
        t.mock.timers.enable({ apis: ['setTimeout'] });
        const fn = t.mock.fn();
        globalThis.setTimeout(fn, -1);
        t.mock.timers.tick(1);
        assert.strictEqual(fn.mock.callCount(), 1);
      });
    });

    describe('clearTimeout Suite', () => {
      it('should not advance in time if clearTimeout was invoked', (t) => {
        t.mock.timers.enable({ apis: ['setTimeout'] });

        const fn = mock.fn();

        const id = globalThis.setTimeout(fn, 4000);
        globalThis.clearTimeout(id);
        t.mock.timers.tick(4000);

        assert.strictEqual(fn.mock.callCount(), 0);
      });

      it('clearTimeout does not throw on null and undefined', (t) => {
        t.mock.timers.enable({ apis: ['setTimeout'] });

        nodeTimers.clearTimeout();
        nodeTimers.clearTimeout(null);
      });
    });

    describe('setInterval Suite', () => {
      it('should tick three times using fake setInterval', (t) => {
        t.mock.timers.enable({ apis: ['setInterval'] });
        const fn = t.mock.fn();

        const id = globalThis.setInterval(fn, 200);

        t.mock.timers.tick(200);
        t.mock.timers.tick(200);
        t.mock.timers.tick(200);

        globalThis.clearInterval(id);

        assert.strictEqual(fn.mock.callCount(), 3);
      });

      it('should work with the same params as the original setInterval', (t) => {
        t.mock.timers.enable({ apis: ['setInterval'] });
        const fn = t.mock.fn();
        const args = ['a', 'b', 'c'];
        const id = globalThis.setInterval(fn, 200, ...args);

        t.mock.timers.tick(200);
        t.mock.timers.tick(200);
        t.mock.timers.tick(200);

        globalThis.clearInterval(id);

        assert.strictEqual(fn.mock.callCount(), 3);
        assert.deepStrictEqual(fn.mock.calls[0].arguments, args);
        assert.deepStrictEqual(fn.mock.calls[1].arguments, args);
        assert.deepStrictEqual(fn.mock.calls[2].arguments, args);
      });
    });

    describe('clearInterval Suite', () => {
      it('should not advance in time if clearInterval was invoked', (t) => {
        t.mock.timers.enable({ apis: ['setInterval'] });

        const fn = mock.fn();
        const id = globalThis.setInterval(fn, 200);
        globalThis.clearInterval(id);
        t.mock.timers.tick(200);

        assert.strictEqual(fn.mock.callCount(), 0);
      });

      it('clearInterval does not throw on null and undefined', (t) => {
        t.mock.timers.enable({ apis: ['setInterval'] });

        nodeTimers.clearInterval();
        nodeTimers.clearInterval(null);
      });
    });

    describe('setImmediate Suite', () => {
      it('should keep setImmediate working if timers are disabled', (t, done) => {
        const now = Date.now();
        const timeout = 2;
        const expected = () => now - timeout;
        globalThis.setImmediate(common.mustCall(() => {
          assert.strictEqual(now - timeout, expected());
          done();
        }));
      });

      it('should work with the same params as the original setImmediate', (t) => {
        t.mock.timers.enable({ apis: ['setImmediate'] });
        const fn = t.mock.fn();
        const args = ['a', 'b', 'c'];
        globalThis.setImmediate(fn, ...args);
        t.mock.timers.tick(9999);

        assert.strictEqual(fn.mock.callCount(), 1);
        assert.deepStrictEqual(fn.mock.calls[0].arguments, args);
      });

      it('should not advance in time if clearImmediate was invoked', (t) => {
        t.mock.timers.enable({ apis: ['setImmediate'] });

        const id = globalThis.setImmediate(common.mustNotCall());
        globalThis.clearImmediate(id);
        t.mock.timers.tick(200);
      });

      it('should advance in time and trigger timers when calling the .tick function', (t) => {
        t.mock.timers.enable({ apis: ['setImmediate'] });
        globalThis.setImmediate(common.mustCall(1));
        t.mock.timers.tick(0);
      });

      it('should execute in order if setImmediate is called multiple times', (t) => {
        t.mock.timers.enable({ apis: ['setImmediate'] });
        const order = [];
        const fn1 = t.mock.fn(common.mustCall(() => order.push('f1'), 1));
        const fn2 = t.mock.fn(common.mustCall(() => order.push('f2'), 1));

        globalThis.setImmediate(fn1);
        globalThis.setImmediate(fn2);

        t.mock.timers.tick(0);

        assert.deepStrictEqual(order, ['f1', 'f2']);
      });

      it('should execute setImmediate first if setTimeout was also called', (t) => {
        t.mock.timers.enable({ apis: ['setImmediate', 'setTimeout'] });
        const order = [];
        const fn1 = t.mock.fn(common.mustCall(() => order.push('f1'), 1));
        const fn2 = t.mock.fn(common.mustCall(() => order.push('f2'), 1));

        globalThis.setTimeout(fn2, 0);
        globalThis.setImmediate(fn1);

        t.mock.timers.tick(100);

        assert.deepStrictEqual(order, ['f1', 'f2']);
      });
    });

    describe('clearImmediate Suite', () => {
      it('clearImmediate does not throw on null and undefined', (t) => {
        t.mock.timers.enable({ apis: ['setImmediate'] });

        nodeTimers.clearImmediate();
        nodeTimers.clearImmediate(null);
      });
    });

    describe('timers/promises', () => {
      describe('setTimeout Suite', () => {
        it('should advance in time and trigger timers when calling the .tick function multiple times', async (t) => {
          t.mock.timers.enable({ apis: ['setTimeout'] });

          const p = nodeTimersPromises.setTimeout(2000);

          t.mock.timers.tick(1000);
          t.mock.timers.tick(500);
          t.mock.timers.tick(500);
          t.mock.timers.tick(500);

          p.then(common.mustCall((result) => {
            assert.strictEqual(result, undefined);
          }));
        });

        it('should work with the same params as the original timers/promises/setTimeout', async (t) => {
          t.mock.timers.enable({ apis: ['setTimeout'] });
          const expectedResult = 'result';
          const controller = new AbortController();
          const p = nodeTimersPromises.setTimeout(2000, expectedResult, {
            ref: true,
            signal: controller.signal,
          });

          t.mock.timers.tick(1000);
          t.mock.timers.tick(500);
          t.mock.timers.tick(500);
          t.mock.timers.tick(500);

          const result = await p;
          assert.strictEqual(result, expectedResult);
        });

        it('should always return the same result as the original timers/promises/setTimeout', async (t) => {
          t.mock.timers.enable({ apis: ['setTimeout'] });
          for (const expectedResult of [undefined, null, false, true, 0, 0n, 1, 1n, '', 'result', {}]) {
            const p = nodeTimersPromises.setTimeout(2000, expectedResult);
            t.mock.timers.tick(2000);
            const result = await p;
            assert.strictEqual(result, expectedResult);
          }
        });

        it('should abort operation if timers/promises/setTimeout received an aborted signal', async (t) => {
          t.mock.timers.enable({ apis: ['setTimeout'] });
          const expectedResult = 'result';
          const controller = new AbortController();
          const p = nodeTimersPromises.setTimeout(2000, expectedResult, {
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
          t.mock.timers.enable({ apis: ['setTimeout'] });
          const expectedResult = 'result';
          const controller = new AbortController();
          const p = nodeTimersPromises.setTimeout(2000, expectedResult, {
            ref: true,
            signal: controller.signal,
          });

          controller.abort();

          await assert.rejects(() => p, {
            name: 'AbortError',
          });
        });

        it('should abort operation when .abort is called before calling setInterval', async (t) => {
          t.mock.timers.enable({ apis: ['setTimeout'] });
          const expectedResult = 'result';
          const controller = new AbortController();
          controller.abort();
          const p = nodeTimersPromises.setTimeout(2000, expectedResult, {
            ref: true,
            signal: controller.signal,
          });

          await assert.rejects(() => p, {
            name: 'AbortError',
          });
        });

        it('should reject given an an invalid signal instance', async (t) => {
          t.mock.timers.enable({ apis: ['setTimeout'] });
          const expectedResult = 'result';
          const p = nodeTimersPromises.setTimeout(2000, expectedResult, {
            ref: true,
            signal: {},
          });

          await assert.rejects(() => p, {
            name: 'TypeError',
            code: 'ERR_INVALID_ARG_TYPE',
          });
        });

        // Test for https://github.com/nodejs/node/issues/50365
        it('should not affect other timers when aborting', async (t) => {
          const f1 = t.mock.fn();
          const f2 = t.mock.fn();
          t.mock.timers.enable({ apis: ['setTimeout'] });
          const ac = new AbortController();

          // id 1 & pos 1 in priority queue
          nodeTimersPromises.setTimeout(100, undefined, { signal: ac.signal }).then(f1, f1);
          // id 2 & pos 1 in priority queue (id 1 is moved to pos 2)
          nodeTimersPromises.setTimeout(50).then(f2, f2);

          ac.abort(); // BUG: will remove timer at pos 1 not timer with id 1!

          t.mock.timers.runAll();
          await nodeTimersPromises.setImmediate(); // let promises settle

          // First setTimeout is aborted
          assert.strictEqual(f1.mock.callCount(), 1);
          assert.strictEqual(f1.mock.calls[0].arguments[0].code, 'ABORT_ERR');

          // Second setTimeout should resolve, but never settles, because it was eronously removed by ac.abort()
          assert.strictEqual(f2.mock.callCount(), 1);
        });

        // Test for https://github.com/nodejs/node/issues/50365
        it('should not affect other timers when aborted after triggering', async (t) => {
          const f1 = t.mock.fn();
          const f2 = t.mock.fn();
          t.mock.timers.enable({ apis: ['setTimeout'] });
          const ac = new AbortController();

          // id 1 & pos 1 in priority queue
          nodeTimersPromises.setTimeout(50, true, { signal: ac.signal }).then(f1, f1);
          // id 2 & pos 2 in priority queue
          nodeTimersPromises.setTimeout(100).then(f2, f2);

          // First setTimeout resolves
          t.mock.timers.tick(50);
          await nodeTimersPromises.setImmediate(); // let promises settle
          assert.strictEqual(f1.mock.callCount(), 1);
          assert.strictEqual(f1.mock.calls[0].arguments.length, 1);
          assert.strictEqual(f1.mock.calls[0].arguments[0], true);

          // Now timer with id 2 will be at pos 1 in priority queue
          ac.abort(); // BUG: will remove timer at pos 1 not timer with id 1!

          // Second setTimeout should resolve, but never settles, because it was eronously removed by ac.abort()
          t.mock.timers.runAll();
          await nodeTimersPromises.setImmediate(); // let promises settle
          assert.strictEqual(f2.mock.callCount(), 1);
        });

        it('should not affect other timers when clearing timeout inside own callback', (t) => {
          t.mock.timers.enable({ apis: ['setTimeout'] });
          const f = t.mock.fn();

          const timer = nodeTimers.setTimeout(() => {
            f();
            // Clearing the already-expired timeout should do nothing
            nodeTimers.clearTimeout(timer);
          }, 50);
          nodeTimers.setTimeout(f, 50);
          nodeTimers.setTimeout(f, 50);

          t.mock.timers.runAll();
          assert.strictEqual(f.mock.callCount(), 3);
        });

        it('should allow clearing timeout inside own callback', (t) => {
          t.mock.timers.enable({ apis: ['setTimeout'] });
          const f = t.mock.fn();

          const timer = nodeTimers.setTimeout(() => {
            f();
            nodeTimers.clearTimeout(timer);
          }, 50);

          t.mock.timers.runAll();
          assert.strictEqual(f.mock.callCount(), 1);
        });
      });

      describe('setInterval Suite', () => {
        it('should tick three times using fake setInterval', async (t) => {
          t.mock.timers.enable({ apis: ['setInterval'] });

          const interval = 100;
          const intervalIterator = nodeTimersPromises.setInterval(interval, Date.now());

          const first = intervalIterator.next();
          const second = intervalIterator.next();
          const third = intervalIterator.next();

          t.mock.timers.tick(interval);
          t.mock.timers.tick(interval);
          t.mock.timers.tick(interval);
          t.mock.timers.tick(interval);

          const results = await Promise.all([
            first,
            second,
            third,
          ]);

          const finished = await intervalIterator.return();
          assert.deepStrictEqual(finished, { done: true, value: undefined });
          for (const result of results) {
            assert.strictEqual(typeof result.value, 'number');
            assert.strictEqual(result.done, false);
          }
        });
        it('should tick five times testing a real use case', async (t) => {
          t.mock.timers.enable({ apis: ['setInterval'] });

          const expectedIterations = 5;
          const interval = 1000;
          let time = 0;
          async function run() {
            const times = [];
            for await (const _ of nodeTimersPromises.setInterval(interval)) { // eslint-disable-line no-unused-vars
              time += interval;
              times.push(time);
              if (times.length === expectedIterations) break;
            }
            return times;
          }

          const r = run();
          t.mock.timers.tick(interval);
          t.mock.timers.tick(interval);
          t.mock.timers.tick(interval);
          t.mock.timers.tick(interval);
          t.mock.timers.tick(interval);

          const timeResults = await r;
          assert.strictEqual(timeResults.length, expectedIterations);
          for (let it = 1; it < expectedIterations; it++) {
            assert.strictEqual(timeResults[it - 1], interval * it);
          }
        });

        it('should always return the same result as the original timers/promises/setInterval', async (t) => {
          t.mock.timers.enable({ apis: ['setInterval'] });
          for (const expectedResult of [undefined, null, false, true, 0, 0n, 1, 1n, '', 'result', {}]) {
            const intervalIterator = nodeTimersPromises.setInterval(2000, expectedResult);
            const p = intervalIterator.next();
            t.mock.timers.tick(2000);
            const result = await p;
            await intervalIterator.return();
            assert.strictEqual(result.done, false);
            assert.strictEqual(result.value, expectedResult);
          }
        });

        it('should abort operation given an abort controller signal', async (t) => {
          t.mock.timers.enable({ apis: ['setInterval'] });

          const interval = 100;
          const abortController = new AbortController();
          const intervalIterator = nodeTimersPromises.setInterval(interval, Date.now(), {
            signal: abortController.signal,
          });

          const first = intervalIterator.next();
          const second = intervalIterator.next();

          t.mock.timers.tick(interval);
          abortController.abort();
          t.mock.timers.tick(interval);

          const firstResult = await first;
          // Interval * 2 because value can be a little bit greater than interval
          assert.ok(firstResult.value < Date.now() + interval * 2);
          assert.strictEqual(firstResult.done, false);

          await assert.rejects(() => second, {
            name: 'AbortError',
          });
        });

        it('should abort operation when .abort is called before calling setInterval', async (t) => {
          t.mock.timers.enable({ apis: ['setInterval'] });

          const interval = 100;
          const abortController = new AbortController();
          abortController.abort();
          const intervalIterator = nodeTimersPromises.setInterval(interval, Date.now(), {
            signal: abortController.signal,
          });

          const first = intervalIterator.next();
          t.mock.timers.tick(interval);

          await assert.rejects(() => first, {
            name: 'AbortError',
          });
        });

        it('should abort operation given an abort controller signal on a real use case', async (t) => {
          t.mock.timers.enable({ apis: ['setInterval'] });
          const controller = new AbortController();
          const signal = controller.signal;
          const interval = 200;
          const expectedIterations = 2;
          let numIterations = 0;
          async function run() {
            const it = nodeTimersPromises.setInterval(interval, undefined, { signal });
            for await (const _ of it) { // eslint-disable-line no-unused-vars
              numIterations += 1;
              if (numIterations === 5) break;
            }
          }

          const r = run();
          t.mock.timers.tick(interval);
          t.mock.timers.tick(interval);
          controller.abort();
          t.mock.timers.tick(interval);
          t.mock.timers.tick(interval);
          t.mock.timers.tick(interval);
          t.mock.timers.tick(interval);

          await assert.rejects(() => r, {
            name: 'AbortError',
          });
          assert.strictEqual(numIterations, expectedIterations);
        });

        // Test for https://github.com/nodejs/node/issues/50381
        it('should use the mocked interval', (t) => {
          t.mock.timers.enable({ apis: ['setInterval'] });
          const fn = t.mock.fn();
          setInterval(fn, 1000);
          assert.strictEqual(fn.mock.callCount(), 0);
          t.mock.timers.tick(1000);
          assert.strictEqual(fn.mock.callCount(), 1);
          t.mock.timers.tick(1);
          t.mock.timers.tick(1);
          t.mock.timers.tick(1);
          assert.strictEqual(fn.mock.callCount(), 1);
        });

        // Test for https://github.com/nodejs/node/issues/50382
        it('should not prevent due timers to be processed', async (t) => {
          t.mock.timers.enable({ apis: ['setInterval', 'setTimeout'] });
          const f1 = t.mock.fn();
          const f2 = t.mock.fn();

          setInterval(f1, 1000);
          setTimeout(f2, 1001);

          assert.strictEqual(f1.mock.callCount(), 0);
          assert.strictEqual(f2.mock.callCount(), 0);

          t.mock.timers.tick(1001);

          assert.strictEqual(f1.mock.callCount(), 1);
          assert.strictEqual(f2.mock.callCount(), 1);
        });
      });
    });
  });

  describe('Api should have same public properties as original', () => {
    it('should have hasRef', (t) => {
      t.mock.timers.enable();
      const timer = setTimeout();
      assert.strictEqual(typeof timer.hasRef, 'function');
      assert.strictEqual(timer.hasRef(), true);
      clearTimeout(timer);
    });

    it('should have ref', (t) => {
      t.mock.timers.enable();
      const timer = setTimeout();
      assert.ok(typeof timer.ref === 'function');
      assert.deepStrictEqual(timer.ref(), timer);
      clearTimeout(timer);
    });

    it('should have unref', (t) => {
      t.mock.timers.enable();
      const timer = setTimeout();
      assert.ok(typeof timer.unref === 'function');
      assert.deepStrictEqual(timer.unref(), timer);
      clearTimeout(timer);
    });

    it('should have refresh', (t) => {
      t.mock.timers.enable();
      const timer = setTimeout();
      assert.ok(typeof timer.refresh === 'function');
      assert.deepStrictEqual(timer.refresh(), timer);
      clearTimeout(timer);
    });
  });
});
