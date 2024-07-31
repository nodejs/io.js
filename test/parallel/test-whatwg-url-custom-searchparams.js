'use strict';

// Tests below are not from WPT.

require('../common');
const assert = require('assert');
const fixtures = require('../common/fixtures');

const serialized = 'a=a&a=1&a=true&a=undefined&a=null&a=%EF%BF%BD' +
                   '&a=%EF%BF%BD&a=%F0%9F%98%80&a=%EF%BF%BD%EF%BF%BD' +
                   '&a=%5Bobject+Object%5D';
const values = ['a', 1, true, undefined, null, '\uD83D', '\uDE00',
                '\uD83D\uDE00', '\uDE00\uD83D', {}];
const normalizedValues = ['a', '1', 'true', 'undefined', 'null', '\uFFFD',
                          '\uFFFD', '\uD83D\uDE00', '\uFFFD\uFFFD',
                          '[object Object]'];

const m = new URL('http://example.org');
const ownSymbolsBeforeGetterAccess = Object.getOwnPropertySymbols(m);
const sp = m.searchParams;
assert.deepStrictEqual(Object.getOwnPropertySymbols(m), ownSymbolsBeforeGetterAccess);

assert(sp);
assert.strictEqual(sp.toString(), '');
assert.strictEqual(m.search, '');

assert(!sp.has('a'));
values.forEach((i) => sp.set('a', i));
assert(sp.has('a'));
assert.strictEqual(sp.get('a'), '[object Object]');
sp.delete('a');
assert(!sp.has('a'));

m.search = '';
assert.strictEqual(sp.toString(), '');

values.forEach((i) => sp.append('a', i));
assert(sp.has('a'));
assert.strictEqual(sp.getAll('a').length, values.length);
assert.strictEqual(sp.get('a'), 'a');

assert.strictEqual(sp.toString(), serialized);

assert.strictEqual(m.search, `?${serialized}`);

sp.delete('a');
values.forEach((i) => sp.append('a', i));
assert.strictEqual(m.href, `http://example.org/?${serialized}`);

sp.delete('a');
values.forEach((i) => sp.append('a', i));
assert.strictEqual(m.toString(), `http://example.org/?${serialized}`);

sp.delete('a');
values.forEach((i) => sp.append('a', i));
assert.strictEqual(m.toJSON(), `http://example.org/?${serialized}`);

sp.delete('a');
values.forEach((i) => sp.append('a', i));
m.href = 'http://example.org';
assert.strictEqual(m.href, 'http://example.org/');
assert.strictEqual(sp.size, 0);

sp.delete('a');
values.forEach((i) => sp.append('a', i));
m.search = '';
assert.strictEqual(m.href, 'http://example.org/');
assert.strictEqual(sp.size, 0);

sp.delete('a');
values.forEach((i) => sp.append('a', i));
m.pathname = '/test';
assert.strictEqual(m.href, `http://example.org/test?${serialized}`);
m.pathname = '';

sp.delete('a');
values.forEach((i) => sp.append('a', i));
m.hash = '#test';
assert.strictEqual(m.href, `http://example.org/?${serialized}#test`);
m.hash = '';

assert.strictEqual(sp[Symbol.iterator], sp.entries);

let key, val;
let n = 0;
for ([key, val] of sp) {
  assert.strictEqual(key, 'a', n);
  assert.strictEqual(val, normalizedValues[n], n);
  n++;
}
n = 0;
for (key of sp.keys()) {
  assert.strictEqual(key, 'a', n);
  n++;
}
n = 0;
for (val of sp.values()) {
  assert.strictEqual(val, normalizedValues[n], n);
  n++;
}
n = 0;
sp.forEach(function(val, key, obj) {
  assert.strictEqual(this, undefined, n);
  assert.strictEqual(key, 'a', n);
  assert.strictEqual(val, normalizedValues[n], n);
  assert.strictEqual(obj, sp, n);
  n++;
});
sp.forEach(function() {
  assert.strictEqual(this, m);
}, m);

{
  const callbackErr = {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError'
  };
  assert.throws(() => sp.forEach(), callbackErr);
  assert.throws(() => sp.forEach(1), callbackErr);
}

m.search = '?a=a&b=b';
assert.strictEqual(sp.toString(), 'a=a&b=b');

const tests = require(fixtures.path('url-searchparams.js'));

for (const [input, expected, parsed] of tests) {
  if (input[0] !== '?') {
    const sp = new URLSearchParams(input);
    assert.strictEqual(String(sp), expected);
    assert.deepStrictEqual(Array.from(sp), parsed);

    m.search = input;
    assert.strictEqual(String(m.searchParams), expected);
    assert.deepStrictEqual(Array.from(m.searchParams), parsed);
  }

  {
    const sp = new URLSearchParams(`?${input}`);
    assert.strictEqual(String(sp), expected);
    assert.deepStrictEqual(Array.from(sp), parsed);

    m.search = `?${input}`;
    assert.strictEqual(String(m.searchParams), expected);
    assert.deepStrictEqual(Array.from(m.searchParams), parsed);
  }
}
