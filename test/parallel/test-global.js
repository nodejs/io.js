// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

// This test cannot run in strict mode because it tests that `baseFoo` is
// treated as a global without being declared with `var`/`let`/`const`.

/* eslint-disable strict */
const common = require('../common');
const fixtures = require('../common/fixtures');

const assert = require('assert');
const { builtinModules } = require('module');

// Load all modules to actually cover most code parts.
for (const moduleName of builtinModules) {
  if (!moduleName.includes('/')) {
    try {
      // This could throw for e.g., crypto if the binary is not compiled
      // accordingly.
      require(moduleName);
    } catch {
      // Continue regardless of error.
    }
  }
}

{
  const expected = [
    'global',
    'queueMicrotask',
    'clearImmediate',
    'clearInterval',
    'clearTimeout',
    'atob',
    'btoa',
    'performance',
    'setImmediate',
    'setInterval',
    'setTimeout',
    'structuredClone',
    'fetch',
    'crypto',
    'navigator',
  ];
  assert.deepStrictEqual(new Set(Object.keys(globalThis)), new Set(expected));
  expected.forEach((value) => {
    const desc = Object.getOwnPropertyDescriptor(globalThis, value);
    if (typeof desc.value === 'function') {
      assert.strictEqual(desc.value.name, value);
    } else if (typeof desc.get === 'function') {
      assert.strictEqual(desc.get.name, `get ${value}`);
    }
  });
}

common.allowGlobals('bar', 'foo');

baseFoo = 'foo'; // eslint-disable-line no-undef
globalThis.baseBar = 'bar';

assert.strictEqual(globalThis.baseFoo, 'foo',
                   `x -> globalThis.x failed: globalThis.baseFoo = ${globalThis.baseFoo}`);

assert.strictEqual(baseBar, // eslint-disable-line no-undef
                   'bar',
                   // eslint-disable-next-line no-undef
                   `globalThis.x -> x failed: baseBar = ${baseBar}`);

const mod = require(fixtures.path('global', 'plain'));
const fooBar = mod.fooBar;

assert.strictEqual(fooBar.foo, 'foo');

assert.strictEqual(fooBar.bar, 'bar');

assert.strictEqual(Object.prototype.toString.call(globalThis), '[object global]');
