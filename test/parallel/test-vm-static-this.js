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

/* eslint-disable strict */
const common = require('../common');
const assert = require('assert');
const vm = require('vm');

// Run a string
const result = vm.runInThisContext('\'passed\';');
assert.strictEqual(result, 'passed');

// thrown error
assert.throws(function() {
  vm.runInThisContext('throw new Error(\'test\');');
}, /test/);

globalThis.hello = 5;
vm.runInThisContext('hello = 2');
assert.strictEqual(globalThis.hello, 2);


// pass values
const code = 'foo = 1;' +
             'bar = 2;' +
             'if (typeof baz !== \'undefined\')' +
             'throw new Error(\'test fail\');';
globalThis.foo = 2;
globalThis.obj = { foo: 0, baz: 3 };
/* eslint-disable no-unused-vars */
const baz = vm.runInThisContext(code);
/* eslint-enable no-unused-vars */
assert.strictEqual(globalThis.obj.foo, 0);
assert.strictEqual(globalThis.bar, 2);
assert.strictEqual(globalThis.foo, 1);

// call a function
globalThis.f = function() { globalThis.foo = 100; };
vm.runInThisContext('f()');
assert.strictEqual(globalThis.foo, 100);

common.allowGlobals(
  globalThis.hello,
  globalThis.foo,
  globalThis.obj,
  globalThis.f
);
