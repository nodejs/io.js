'use strict';
process.env.NODE_TEST_KNOWN_GLOBALS = 0;
require('../common');

const assert = require('node:assert');
const { it, describe } = require('node:test');

describe('Mock Timers Date Test Suite', () => {
  it('should return the initial UNIX epoch if not specified', (t) => {
    t.mock.timers.enable({ apis: ['Date'] });
    const date = new Date();
    assert.strictEqual(date.getTime(), 0);
    assert.strictEqual(Date.now(), 0);
  });

  it('should throw an error if setTime is called without enabling timers', (t) => {
    assert.throws(
      () => {
        t.mock.timers.setTime(100);
      },
      { code: 'ERR_INVALID_STATE' }
    );
  });

  it('should throw an error if epoch passed to enable is not valid', (t) => {
    assert.throws(
      () => {
        t.mock.timers.enable({ now: -1 });
      },
      { code: 'ERR_INVALID_ARG_VALUE' }
    );

    assert.throws(
      () => {
        t.mock.timers.enable({ now: 'string' });
      },
      { code: 'ERR_INVALID_ARG_TYPE' }
    );

    assert.throws(
      () => {
        t.mock.timers.enable({ now: NaN });
      },
      { code: 'ERR_INVALID_ARG_VALUE' }
    );
  });

  it('should replace the original Date with the mocked one', (t) => {
    t.mock.timers.enable({ apis: ['Date'] });
    assert.ok(Date.isMock);
  });

  it('should return the ticked time when calling Date.now after tick', (t) => {
    t.mock.timers.enable({ apis: ['Date'] });
    const time = 100;
    t.mock.timers.tick(time);
    assert.strictEqual(Date.now(), time);
  });

  it('should return the Date as string when calling it as a function', (t) => {
    t.mock.timers.enable({ apis: ['Date'] });
    const returned = Date();
    // Matches the format: 'Mon Jan 01 1970 00:00:00'
    // We don't care about the date, just the format
    assert.ok(/\w{3}\s\w{3}\s\d{1,2}\s\d{2,4}\s\d{1,2}:\d{2}:\d{2}/.test(returned));
  });

  it('should return the date with different argument calls', (t) => {
    t.mock.timers.enable({ apis: ['Date'] });
    assert.strictEqual(new Date(0).getTime(), 0);
    assert.strictEqual(new Date(100).getTime(), 100);
    assert.strictEqual(new Date('1970-01-01T00:00:00.000Z').getTime(), 0);
    assert.strictEqual(new Date(1970, 0).getFullYear(), 1970);
    assert.strictEqual(new Date(1970, 0).getMonth(), 0);
    assert.strictEqual(new Date(1970, 0, 1).getDate(), 1);
    assert.strictEqual(new Date(1970, 0, 1, 11).getHours(), 11);
    assert.strictEqual(new Date(1970, 0, 1, 11, 10).getMinutes(), 10);
    assert.strictEqual(new Date(1970, 0, 1, 11, 10, 45).getSeconds(), 45);
    assert.strictEqual(new Date(1970, 0, 1, 11, 10, 45, 898).getMilliseconds(), 898);
    assert.strictEqual(new Date(1970, 0, 1, 11, 10, 45, 898).toDateString(), 'Thu Jan 01 1970');
  });

  it('should return native code when calling Date.toString', (t) => {
    t.mock.timers.enable({ apis: ['Date'] });
    assert.strictEqual(Date.toString(), 'function Date() { [native code] }');
  });

  it('should start with a custom epoch if the second argument is specified', (t) => {
    t.mock.timers.enable({ apis: ['Date'], now: 100 });
    const date1 = new Date();
    assert.strictEqual(date1.getTime(), 100);

    t.mock.timers.reset();
    t.mock.timers.enable({ apis: ['Date'], now: new Date(200) });
    const date2 = new Date();
    assert.strictEqual(date2.getTime(), 200);
  });

  it('should replace epoch if setTime is lesser than now and not tick', (t) => {
    t.mock.timers.enable();
    const fn = t.mock.fn();
    const id = setTimeout(fn, 1000);
    t.mock.timers.setTime(800);
    assert.strictEqual(Date.now(), 800);
    t.mock.timers.setTime(500);
    assert.strictEqual(Date.now(), 500);
    assert.strictEqual(fn.mock.callCount(), 0);
    clearTimeout(id);
  });

  it('should not tick time when setTime is called', (t) => {
    t.mock.timers.enable();
    const fn = t.mock.fn();
    const id = setTimeout(fn, 1000);
    t.mock.timers.setTime(1200);
    assert.strictEqual(Date.now(), 1200);
    assert.strictEqual(fn.mock.callCount(), 0);
    clearTimeout(id);
  });

  it((t) => {
    t.mock.timers.enable();
    t.test('should throw when a already-mocked Date is mocked', (t2) => {
      assert.throws(() => t2.mock.timers.enable(), { code: 'ERR_INVALID_STATE' });
    });
  });
});
