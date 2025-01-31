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
require('../common');
const assert = require('assert');

const vm = require('vm');

// Run in a new empty context
let context = vm.createContext();
let result = vm.runInContext('"passed";', context);
assert.strictEqual(result, 'passed');

// Create a new pre-populated context
context = vm.createContext({ 'foo': 'bar', 'thing': 'lala' });
assert.strictEqual(context.foo, 'bar');
assert.strictEqual(context.thing, 'lala');

// Test updating context
result = vm.runInContext('var foo = 3;', context);
assert.strictEqual(context.foo, 3);
assert.strictEqual(context.thing, 'lala');

// https://github.com/nodejs/node/issues/5768
// Run in contextified sandbox without referencing the context
const sandbox = { x: 1 };
vm.createContext(sandbox);
globalThis.gc();
vm.runInContext('x = 2', sandbox);
// Should not crash.
