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

'use strict';
// Flags: --expose-gc

const common = require('../common');
const assert = require('assert');
const vm = require('vm');

if (typeof globalThis.gc !== 'function')
  assert.fail('Run this test with --expose-gc');

// Run a string
const result = vm.runInNewContext('\'passed\';');
assert.strictEqual(result, 'passed');

// Thrown error
assert.throws(() => {
  vm.runInNewContext('throw new Error(\'test\');');
}, /^Error: test$/);

globalThis.hello = 5;
vm.runInNewContext('hello = 2');
assert.strictEqual(globalThis.hello, 5);


// Pass values in and out
globalThis.code = 'foo = 1;' +
              'bar = 2;' +
              'if (baz !== 3) throw new Error(\'test fail\');';
globalThis.foo = 2;
globalThis.obj = { foo: 0, baz: 3 };
/* eslint-disable no-unused-vars */
const baz = vm.runInNewContext(globalThis.code, globalThis.obj);
/* eslint-enable no-unused-vars */
assert.strictEqual(globalThis.obj.foo, 1);
assert.strictEqual(globalThis.obj.bar, 2);
assert.strictEqual(globalThis.foo, 2);

// Call a function by reference
function changeFoo() { globalThis.foo = 100; }
vm.runInNewContext('f()', { f: changeFoo });
assert.strictEqual(globalThis.foo, 100);

// Modify an object by reference
const f = { a: 1 };
vm.runInNewContext('f.a = 2', { f });
assert.strictEqual(f.a, 2);

// Use function in context without referencing context
const fn = vm.runInNewContext('(function() { obj.p = {}; })', { obj: {} });
globalThis.gc();
fn();
// Should not crash

const filename = 'test_file.vm';
for (const arg of [filename, { filename }]) {
  // Verify that providing a custom filename works.
  const code = 'throw new Error("foo");';

  assert.throws(() => {
    vm.runInNewContext(code, {}, arg);
  }, (err) => {
    const lines = err.stack.split('\n');

    assert.strictEqual(lines[0].trim(), `${filename}:1`);
    assert.strictEqual(lines[1].trim(), code);
    // Skip lines[2] and lines[3]. They're just a ^ and blank line.
    assert.strictEqual(lines[4].trim(), 'Error: foo');
    assert.strictEqual(lines[5].trim(), `at ${filename}:1:7`);
    // The rest of the stack is uninteresting.
    return true;
  });
}

common.allowGlobals(
  globalThis.hello,
  globalThis.code,
  globalThis.foo,
  globalThis.obj
);
