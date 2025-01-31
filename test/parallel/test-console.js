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
const common = require('../common');
const assert = require('assert');
const util = require('util');

const {
  hijackStdout,
  hijackStderr,
  restoreStdout,
  restoreStderr
} = require('../common/hijackstdio');

const { isMainThread } = require('worker_threads');

assert.ok(process.stdout.writable);
assert.ok(process.stderr.writable);
// Support legacy API
if (isMainThread) {
  assert.strictEqual(typeof process.stdout.fd, 'number');
  assert.strictEqual(typeof process.stderr.fd, 'number');
}

common.expectWarning(
  'Warning',
  [
    ['Count for \'noLabel\' does not exist'],
    ['No such label \'noLabel\' for console.timeLog()'],
    ['No such label \'noLabel\' for console.timeEnd()'],
    ['Count for \'default\' does not exist'],
    ['No such label \'default\' for console.timeLog()'],
    ['No such label \'default\' for console.timeEnd()'],
    ['Label \'default\' already exists for console.time()'],
    ['Label \'test\' already exists for console.time()'],
  ]
);

console.countReset('noLabel');
console.timeLog('noLabel');
console.timeEnd('noLabel');

console.time('label');
console.timeEnd('label');

// Test using the default label
// on console.time(), console.countReset(), console.timeLog(), console.timeEnd()
console.countReset();
console.timeLog();
console.timeEnd();

console.time();
console.time();
console.timeLog();
console.timeEnd();

// Check that the `Error` is a `TypeError` but do not check the message as it
// will be different in different JavaScript engines.
assert.throws(() => console.time(Symbol('test')),
              TypeError);
assert.throws(() => console.timeEnd(Symbol('test')),
              TypeError);


// An Object with a custom inspect function.
const custom_inspect = { foo: 'bar', [util.inspect.custom]: () => 'inspect' };

const strings = [];
const errStrings = [];
process.stdout.isTTY = false;
hijackStdout(function(data) {
  strings.push(data);
});
process.stderr.isTTY = false;
hijackStderr(function(data) {
  errStrings.push(data);
});

// Test console.log() goes to stdout
console.log('foo');
console.log('foo', 'bar');
console.log('%s %s', 'foo', 'bar', 'hop');
console.log({ slashes: '\\\\' });
console.log(custom_inspect);

// Test console.debug() goes to stdout
console.debug('foo');
console.debug('foo', 'bar');
console.debug('%s %s', 'foo', 'bar', 'hop');
console.debug({ slashes: '\\\\' });
console.debug(custom_inspect);

// Test console.info() goes to stdout
console.info('foo');
console.info('foo', 'bar');
console.info('%s %s', 'foo', 'bar', 'hop');
console.info({ slashes: '\\\\' });
console.info(custom_inspect);

// Test console.error() goes to stderr
console.error('foo');
console.error('foo', 'bar');
console.error('%s %s', 'foo', 'bar', 'hop');
console.error({ slashes: '\\\\' });
console.error(custom_inspect);

// Test console.warn() goes to stderr
console.warn('foo');
console.warn('foo', 'bar');
console.warn('%s %s', 'foo', 'bar', 'hop');
console.warn({ slashes: '\\\\' });
console.warn(custom_inspect);

// test console.dir()
console.dir(custom_inspect);
console.dir(custom_inspect, { showHidden: false });
console.dir({ foo: { bar: { baz: true } } }, { depth: 0 });
console.dir({ foo: { bar: { baz: true } } }, { depth: 1 });

// Test console.dirxml()
console.dirxml(custom_inspect, custom_inspect);
console.dirxml(
  { foo: { bar: { baz: true } } },
  { foo: { bar: { quux: false } } },
  { foo: { bar: { quux: true } } }
);

// Test console.trace()
console.trace('This is a %j %d', { formatted: 'trace' }, 10, 'foo');

// Test console.time() and console.timeEnd() output
console.time('label');
console.timeEnd('label');

// Verify that Object.prototype properties can be used as labels
console.time('__proto__');
console.timeEnd('__proto__');
console.time('constructor');
console.timeEnd('constructor');
console.time('hasOwnProperty');
console.timeEnd('hasOwnProperty');

// Verify that values are coerced to strings.
console.time([]);
console.timeEnd([]);
console.time({});
console.timeEnd({});
// Repeat the object call to verify that everything really worked.
console.time({});
console.timeEnd({});
console.time(null);
console.timeEnd(null);
console.time(undefined);
console.timeEnd('default');
console.time('default');
console.timeEnd();
console.time(NaN);
console.timeEnd(NaN);

// Make sure calling time twice without timeEnd doesn't reset the timer.
console.time('test');
const time = console._times.get('test');
setTimeout(() => {
  console.time('test');
  assert.deepStrictEqual(console._times.get('test'), time);
  console.timeEnd('test');
}, 1);

console.time('log1');
console.timeLog('log1');
console.timeLog('log1', 'test');
console.timeLog('log1', {}, [1, 2, 3]);
console.timeEnd('log1');

console.assert(false, '%s should', 'console.assert', 'not throw');
assert.strictEqual(errStrings[errStrings.length - 1],
                   'Assertion failed: console.assert should not throw\n');

console.assert(false);
assert.strictEqual(errStrings[errStrings.length - 1], 'Assertion failed\n');

console.assert(true, 'this should not throw');

console.assert(true);

assert.strictEqual(strings.length, process.stdout.writeTimes);
assert.strictEqual(errStrings.length, process.stderr.writeTimes);
restoreStdout();
restoreStderr();

// Verify that console.timeEnd() doesn't leave dead links
const timesMapSize = console._times.size;
console.time('label1');
console.time('label2');
console.time('label3');
console.timeEnd('label1');
console.timeEnd('label2');
console.timeEnd('label3');
assert.strictEqual(console._times.size, timesMapSize);

const expectedStrings = [
  'foo', 'foo bar', 'foo bar hop', "{ slashes: '\\\\\\\\' }", 'inspect',
];

for (const expected of expectedStrings) {
  assert.strictEqual(strings.shift(), `${expected}\n`);
  assert.strictEqual(errStrings.shift(), `${expected}\n`);
}

for (const expected of expectedStrings) {
  assert.strictEqual(strings.shift(), `${expected}\n`);
  assert.strictEqual(errStrings.shift(), `${expected}\n`);
}

for (const expected of expectedStrings) {
  assert.strictEqual(strings.shift(), `${expected}\n`);
}

assert.strictEqual(strings.shift(),
                   "{\n  foo: 'bar',\n  Symbol(nodejs.util.inspect.custom):" +
                    ' [Function: [nodejs.util.inspect.custom]]\n}\n');
assert.strictEqual(strings.shift(),
                   "{\n  foo: 'bar',\n  Symbol(nodejs.util.inspect.custom):" +
                    ' [Function: [nodejs.util.inspect.custom]]\n}\n');
assert.ok(strings.shift().includes('foo: [Object]'));
assert.strictEqual(strings.shift().includes('baz'), false);
assert.strictEqual(strings.shift(), 'inspect inspect\n');
assert.ok(strings[0].includes('foo: { bar: { baz:'));
assert.ok(strings[0].includes('quux'));
assert.ok(strings.shift().includes('quux: true'));

assert.match(strings.shift().trim(), /^label: \d+(\.\d{1,3})?(ms|s)$/);
assert.match(strings.shift().trim(), /^__proto__: \d+(\.\d{1,3})?(ms|s)$/);
assert.match(strings.shift().trim(), /^constructor: \d+(\.\d{1,3})?(ms|s)$/);
assert.match(strings.shift().trim(), /^hasOwnProperty: \d+(\.\d{1,3})?(ms|s)$/);

// Verify that console.time() coerces label values to strings as expected
assert.match(strings.shift().trim(), /^: \d+(\.\d{1,3})?(ms|s)$/);
assert.match(strings.shift().trim(),
             /^\[object Object\]: \d+(\.\d{1,3})?(ms|s)$/);
assert.match(strings.shift().trim(),
             /^\[object Object\]: \d+(\.\d{1,3})?(ms|s)$/);
assert.match(strings.shift().trim(), /^null: \d+(\.\d{1,3})?(ms|s)$/);
assert.match(strings.shift().trim(), /^default: \d+(\.\d{1,3})?(ms|s)$/);
assert.match(strings.shift().trim(), /^default: \d+(\.\d{1,3})?(ms|s)$/);
assert.match(strings.shift().trim(), /^NaN: \d+(\.\d{1,3})?(ms|s)$/);

assert.match(strings.shift().trim(), /^log1: \d+(\.\d{1,3})?(ms|s)$/);
assert.match(strings.shift().trim(), /^log1: \d+(\.\d{1,3})?(ms|s) test$/);
assert.match(strings.shift().trim(),
             /^log1: \d+(\.\d{1,3})?(ms|s) {} \[ 1, 2, 3 ]$/);
assert.match(strings.shift().trim(), /^log1: \d+(\.\d{1,3})?(ms|s)$/);

// Make sure that we checked all strings
assert.strictEqual(strings.length, 0);

assert.strictEqual(errStrings.shift().split('\n').shift(),
                   'Trace: This is a {"formatted":"trace"} 10 foo');

// Hijack stderr to catch `process.emitWarning` which is using
// `process.nextTick`
hijackStderr(common.mustCall(function(data) {
  restoreStderr();

  // stderr.write will catch sync error, so use `process.nextTick` here
  process.nextTick(function() {
    assert.strictEqual(data.includes('noLabel'), true);
  });
}));
