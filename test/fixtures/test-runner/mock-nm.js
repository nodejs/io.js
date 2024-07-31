'use strict';
const assert = require('node:assert');
const { test } = require('node:test');
const fixture = 'reporter-cjs';

test('mock node_modules dependency', async (t) => {
  const mock = t.mock.module(fixture, {
    namedExports: { fn() { return 42; } },
  });
  let cjsImpl = require(fixture);
  let esmImpl = await import(fixture);

  assert.strictEqual(cjsImpl.fn(), 42);
  assert.strictEqual(esmImpl.fn(), 42);

  mock.restore();
  cjsImpl = require(fixture);
  esmImpl = await import(fixture);
  assert.strictEqual(cjsImpl.fn, undefined);
  assert.strictEqual(esmImpl.fn, undefined);
});
