// Flags: --frozen-intrinsics --jitless
'use strict';
require('../common');
const assert = require('assert');

assert.throws(
  () => Object.defineProperty = 'asdf',
  TypeError
);

// Ensure we can extend Console
{
  class ExtendedConsole extends console.Console {}

  const s = new ExtendedConsole(process.stdout);
  const logs = [];
  s.log = (msg) => logs.push(msg);
  s.log('custom');
  s.log = undefined;
  assert.strictEqual(s.log, undefined);
  assert.strictEqual(logs.length, 1);
  assert.strictEqual(logs[0], 'custom');
}

// Ensure we can write override Object prototype properties on instances
{
  const o = {};
  o.toString = () => 'Custom toString';
  assert.strictEqual(o + 'asdf', 'Custom toStringasdf');
  assert.strictEqual(Object.getOwnPropertyDescriptor(o, 'toString').enumerable,
                     true);
}

// Ensure we can not override globalThis
{
  assert.throws(() => { globalThis.globalThis = null; },
                { name: 'TypeError' });
  assert.strictEqual(globalThis.globalThis, globalThis);
}

// Ensure that we cannot override console properties.
{
  const { log } = console;

  assert.throws(() => { console.log = null; },
                { name: 'TypeError' });
  assert.strictEqual(console.log, log);
}
