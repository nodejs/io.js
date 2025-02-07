'use strict';

// Patch globalThis.console before importing modules that may modify the console
// object.

const tmp = globalThis.console;
globalThis.console = 42;

require('../common');
const assert = require('assert');

// Originally the console had a getter. Test twice to verify it had no side
// effect.
assert.strictEqual(globalThis.console, 42);
assert.strictEqual(globalThis.console, 42);

assert.throws(
  () => console.log('foo'),
  { name: 'TypeError' }
);

globalThis.console = 1;
assert.strictEqual(globalThis.console, 1);
assert.strictEqual(console, 1);

// Reset the console
globalThis.console = tmp;
console.log('foo');
