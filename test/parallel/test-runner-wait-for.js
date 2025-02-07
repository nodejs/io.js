'use strict';
require('../common');
const { suite, test } = require('node:test');

suite('input validation', () => {
  test('throws if condition is not a function', (t) => {
    t.assert.throws(() => {
      t.waitFor(5);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "condition" argument must be of type function/,
    });
  });

  test('throws if options is not an object', (t) => {
    t.assert.throws(() => {
      t.waitFor(() => {}, null);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "options" argument must be of type object/,
    });
  });

  test('throws if options.interval is not a number', (t) => {
    t.assert.throws(() => {
      t.waitFor(() => {}, { interval: 'foo' });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "options\.interval" property must be of type number/,
    });
  });

  test('throws if options.timeout is not a number', (t) => {
    t.assert.throws(() => {
      t.waitFor(() => {}, { timeout: 'foo' });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "options\.timeout" property must be of type number/,
    });
  });
});

test('returns the result of the condition function', async (t) => {
  const result = await t.waitFor(() => {
    return 42;
  });

  t.assert.strictEqual(result, 42);
});

test('returns the result of an async condition function', async (t) => {
  const result = await t.waitFor(async () => {
    return 84;
  });

  t.assert.strictEqual(result, 84);
});

test('errors if the condition times out', async (t) => {
  await t.assert.rejects(async () => {
    await t.waitFor(() => {
      return new Promise(() => {});
    }, {
      interval: 60_000,
      timeout: 1,
    });
  }, {
    message: /waitFor\(\) timed out/,
  });
});

test('polls until the condition returns successfully', async (t) => {
  let count = 0;
  const result = await t.waitFor(() => {
    ++count;
    if (count < 4) {
      throw new Error('resource is not ready yet');
    }

    return 'success';
  }, {
    interval: 1,
    timeout: 60_000,
  });

  t.assert.strictEqual(result, 'success');
  t.assert.strictEqual(count, 4);
});

test('sets last failure as error cause on timeouts', async (t) => {
  const error = new Error('boom');
  await t.assert.rejects(async () => {
    await t.waitFor(() => {
      return new Promise((_, reject) => {
        reject(error);
      });
    });
  }, (err) => {
    t.assert.match(err.message, /timed out/);
    t.assert.strictEqual(err.cause, error);
    return true;
  });
});

test('limits polling if condition takes longer than interval', async (t) => {
  let count = 0;

  function condition() {
    count++;
    return new Promise((resolve) => {
      setTimeout(() => {
        resolve('success');
      }, 200);
    });
  }

  const result = await t.waitFor(condition, {
    interval: 1,
    timeout: 60_000,
  });

  t.assert.strictEqual(result, 'success');
  t.assert.strictEqual(count, 1);
});
