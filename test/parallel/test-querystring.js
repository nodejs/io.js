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
require('../common');
const assert = require('assert');
const inspect = require('util').inspect;

// test using assert
const qs = require('querystring');

function createWithNoPrototype(properties) {
  const noProto = { __proto__: null };
  properties.forEach((property) => {
    noProto[property.key] = property.value;
  });
  return noProto;
}
// Folding block, commented to pass gjslint
// {{{
// [ wonkyQS, canonicalQS, obj ]
const qsTestCases = [
  ['__proto__=1',
   '__proto__=1',
   createWithNoPrototype([{ key: '__proto__', value: '1' }])],
  ['__defineGetter__=asdf',
   '__defineGetter__=asdf',
   JSON.parse('{"__defineGetter__":"asdf"}')],
  ['foo=918854443121279438895193',
   'foo=918854443121279438895193',
   { 'foo': '918854443121279438895193' }],
  ['foo=bar', 'foo=bar', { 'foo': 'bar' }],
  ['foo=bar&foo=quux', 'foo=bar&foo=quux', { 'foo': ['bar', 'quux'] }],
  ['foo=1&bar=2', 'foo=1&bar=2', { 'foo': '1', 'bar': '2' }],
  ['my+weird+field=q1%212%22%27w%245%267%2Fz8%29%3F',
   'my%20weird%20field=q1!2%22\'w%245%267%2Fz8)%3F',
   { 'my weird field': 'q1!2"\'w$5&7/z8)?' }],
  ['foo%3Dbaz=bar', 'foo%3Dbaz=bar', { 'foo=baz': 'bar' }],
  ['foo=baz=bar', 'foo=baz%3Dbar', { 'foo': 'baz=bar' }],
  ['str=foo&arr=1&arr=2&arr=3&somenull=&undef=',
   'str=foo&arr=1&arr=2&arr=3&somenull=&undef=',
   { 'str': 'foo',
     'arr': ['1', '2', '3'],
     'somenull': '',
     'undef': '' }],
  [' foo = bar ', '%20foo%20=%20bar%20', { ' foo ': ' bar ' }],
  ['foo=%zx', 'foo=%25zx', { 'foo': '%zx' }],
  ['foo=%EF%BF%BD', 'foo=%EF%BF%BD', { 'foo': '\ufffd' }],
  // See: https://github.com/joyent/node/issues/1707
  ['hasOwnProperty=x&toString=foo&valueOf=bar&__defineGetter__=baz',
   'hasOwnProperty=x&toString=foo&valueOf=bar&__defineGetter__=baz',
   { hasOwnProperty: 'x',
     toString: 'foo',
     valueOf: 'bar',
     __defineGetter__: 'baz' }],
  // See: https://github.com/joyent/node/issues/3058
  ['foo&bar=baz', 'foo=&bar=baz', { foo: '', bar: 'baz' }],
  ['a=b&c&d=e', 'a=b&c=&d=e', { a: 'b', c: '', d: 'e' }],
  ['a=b&c=&d=e', 'a=b&c=&d=e', { a: 'b', c: '', d: 'e' }],
  ['a=b&=c&d=e', 'a=b&=c&d=e', { 'a': 'b', '': 'c', 'd': 'e' }],
  ['a=b&=&c=d', 'a=b&=&c=d', { 'a': 'b', '': '', 'c': 'd' }],
  ['&&foo=bar&&', 'foo=bar', { foo: 'bar' }],
  ['&', '', {}],
  ['&&&&', '', {}],
  ['&=&', '=', { '': '' }],
  ['&=&=', '=&=', { '': [ '', '' ] }],
  ['=', '=', { '': '' }],
  ['+', '%20=', { ' ': '' }],
  ['+=', '%20=', { ' ': '' }],
  ['+&', '%20=', { ' ': '' }],
  ['=+', '=%20', { '': ' ' }],
  ['+=&', '%20=', { ' ': '' }],
  ['a&&b', 'a=&b=', { 'a': '', 'b': '' }],
  ['a=a&&b=b', 'a=a&b=b', { 'a': 'a', 'b': 'b' }],
  ['&a', 'a=', { 'a': '' }],
  ['&=', '=', { '': '' }],
  ['a&a&', 'a=&a=', { a: [ '', '' ] }],
  ['a&a&a&', 'a=&a=&a=', { a: [ '', '', '' ] }],
  ['a&a&a&a&', 'a=&a=&a=&a=', { a: [ '', '', '', '' ] }],
  ['a=&a=value&a=', 'a=&a=value&a=', { a: [ '', 'value', '' ] }],
  ['foo+bar=baz+quux', 'foo%20bar=baz%20quux', { 'foo bar': 'baz quux' }],
  ['+foo=+bar', '%20foo=%20bar', { ' foo': ' bar' }],
  ['a+', 'a%20=', { 'a ': '' }],
  ['=a+', '=a%20', { '': 'a ' }],
  ['a+&', 'a%20=', { 'a ': '' }],
  ['=a+&', '=a%20', { '': 'a ' }],
  ['%20+', '%20%20=', { '  ': '' }],
  ['=%20+', '=%20%20', { '': '  ' }],
  ['%20+&', '%20%20=', { '  ': '' }],
  ['=%20+&', '=%20%20', { '': '  ' }],
  [null, '', {}],
  [undefined, '', {}],
];

// [ wonkyQS, canonicalQS, obj ]
const qsColonTestCases = [
  ['foo:bar', 'foo:bar', { 'foo': 'bar' }],
  ['foo:bar;foo:quux', 'foo:bar;foo:quux', { 'foo': ['bar', 'quux'] }],
  ['foo:1&bar:2;baz:quux',
   'foo:1%26bar%3A2;baz:quux',
   { 'foo': '1&bar:2', 'baz': 'quux' }],
  ['foo%3Abaz:bar', 'foo%3Abaz:bar', { 'foo:baz': 'bar' }],
  ['foo:baz:bar', 'foo:baz%3Abar', { 'foo': 'baz:bar' }],
];

// [wonkyObj, qs, canonicalObj]
function extendedFunction() {}
extendedFunction.prototype = { a: 'b' };
const qsWeirdObjects = [
  // eslint-disable-next-line node-core/no-unescaped-regexp-dot
  [{ regexp: /./g }, 'regexp=', { 'regexp': '' }],
  // eslint-disable-next-line node-core/no-unescaped-regexp-dot
  [{ regexp: new RegExp('.', 'g') }, 'regexp=', { 'regexp': '' }],
  [{ fn: () => {} }, 'fn=', { 'fn': '' }],
  [{ fn: new Function('') }, 'fn=', { 'fn': '' }],
  [{ math: Math }, 'math=', { 'math': '' }],
  [{ e: extendedFunction }, 'e=', { 'e': '' }],
  [{ d: new Date() }, 'd=', { 'd': '' }],
  [{ d: Date }, 'd=', { 'd': '' }],
  [
    { f: new Boolean(false), t: new Boolean(true) },
    'f=&t=',
    { 'f': '', 't': '' },
  ],
  [{ f: false, t: true }, 'f=false&t=true', { 'f': 'false', 't': 'true' }],
  [{ n: null }, 'n=', { 'n': '' }],
  [{ nan: NaN }, 'nan=', { 'nan': '' }],
  [{ inf: Infinity }, 'inf=', { 'inf': '' }],
  [{ a: [], b: [] }, '', {}],
  [{ a: 1, b: [] }, 'a=1', { 'a': '1' }],
];
// }}}

const vm = require('vm');
const foreignObject = vm.runInNewContext('({"foo": ["bar", "baz"]})');

const qsNoMungeTestCases = [
  ['', {}],
  ['foo=bar&foo=baz', { 'foo': ['bar', 'baz'] }],
  ['foo=bar&foo=baz', foreignObject],
  ['blah=burp', { 'blah': 'burp' }],
  ['a=!-._~\'()*', { 'a': '!-._~\'()*' }],
  ['a=abcdefghijklmnopqrstuvwxyz', { 'a': 'abcdefghijklmnopqrstuvwxyz' }],
  ['a=ABCDEFGHIJKLMNOPQRSTUVWXYZ', { 'a': 'ABCDEFGHIJKLMNOPQRSTUVWXYZ' }],
  ['a=0123456789', { 'a': '0123456789' }],
  ['gragh=1&gragh=3&goo=2', { 'gragh': ['1', '3'], 'goo': '2' }],
  ['frappucino=muffin&goat%5B%5D=scone&pond=moose',
   { 'frappucino': 'muffin', 'goat[]': 'scone', 'pond': 'moose' }],
  ['trololol=yes&lololo=no', { 'trololol': 'yes', 'lololo': 'no' }],
];

const qsUnescapeTestCases = [
  ['there is nothing to unescape here',
   'there is nothing to unescape here'],
  ['there+is+nothing+to+unescape+here',
   'there+is+nothing+to+unescape+here'],
  ['there%20are%20several%20spaces%20that%20need%20to%20be%20unescaped',
   'there are several spaces that need to be unescaped'],
  ['there%2Qare%0-fake%escaped values in%%%%this%9Hstring',
   'there%2Qare%0-fake%escaped values in%%%%this%9Hstring'],
  ['there%2Qare%0-fake%escaped%20values in%%%%this%9Hstring',
   'there%2Qare%0-fake%escaped values in%%%%this%9Hstring'],
  ['%20%21%22%23%24%25%26%27%28%29%2A%2B%2C%2D%2E%2F%30%31%32%33%34%35%36%37',
   ' !"#$%&\'()*+,-./01234567'],
  ['%%2a', '%*'],
  ['%2sf%2a', '%2sf*'],
  ['%2%2af%2a', '%2*f*'],
];

assert.strictEqual(qs.parse('id=918854443121279438895193').id,
                   '918854443121279438895193');

function check(actual, expected, input) {
  assert(!(actual instanceof Object));
  const actualKeys = Object.keys(actual).sort();
  const expectedKeys = Object.keys(expected).sort();
  let msg;
  if (typeof input === 'string') {
    msg = `Input: ${inspect(input)}\n` +
          `Actual keys: ${inspect(actualKeys)}\n` +
          `Expected keys: ${inspect(expectedKeys)}`;
  }
  assert.deepStrictEqual(actualKeys, expectedKeys, msg);
  expectedKeys.forEach((key) => {
    if (typeof input === 'string') {
      msg = `Input: ${inspect(input)}\n` +
            `Key: ${inspect(key)}\n` +
            `Actual value: ${inspect(actual[key])}\n` +
            `Expected value: ${inspect(expected[key])}`;
    } else {
      msg = undefined;
    }
    assert.deepStrictEqual(actual[key], expected[key], msg);
  });
}

// Test that the canonical qs is parsed properly.
qsTestCases.forEach((testCase) => {
  check(qs.parse(testCase[0]), testCase[2], testCase[0]);
});

// Test that the colon test cases can do the same
qsColonTestCases.forEach((testCase) => {
  check(qs.parse(testCase[0], ';', ':'), testCase[2], testCase[0]);
});

// Test the weird objects, that they get parsed properly
qsWeirdObjects.forEach((testCase) => {
  check(qs.parse(testCase[1]), testCase[2], testCase[1]);
});

qsNoMungeTestCases.forEach((testCase) => {
  assert.deepStrictEqual(qs.stringify(testCase[1], '&', '='), testCase[0]);
});

// Test the nested qs-in-qs case
{
  const f = qs.parse('a=b&q=x%3Dy%26y%3Dz');
  check(f, createWithNoPrototype([
    { key: 'a', value: 'b' },
    { key: 'q', value: 'x=y&y=z' },
  ]));

  f.q = qs.parse(f.q);
  const expectedInternal = createWithNoPrototype([
    { key: 'x', value: 'y' },
    { key: 'y', value: 'z' },
  ]);
  check(f.q, expectedInternal);
}

// nested in colon
{
  const f = qs.parse('a:b;q:x%3Ay%3By%3Az', ';', ':');
  check(f, createWithNoPrototype([
    { key: 'a', value: 'b' },
    { key: 'q', value: 'x:y;y:z' },
  ]));
  f.q = qs.parse(f.q, ';', ':');
  const expectedInternal = createWithNoPrototype([
    { key: 'x', value: 'y' },
    { key: 'y', value: 'z' },
  ]);
  check(f.q, expectedInternal);
}

// Now test stringifying

// basic
qsTestCases.forEach((testCase) => {
  assert.strictEqual(qs.stringify(testCase[2]), testCase[1]);
});

qsColonTestCases.forEach((testCase) => {
  assert.strictEqual(qs.stringify(testCase[2], ';', ':'), testCase[1]);
});

qsWeirdObjects.forEach((testCase) => {
  assert.strictEqual(qs.stringify(testCase[0]), testCase[1]);
});

// BigInt values

assert.strictEqual(qs.stringify({ foo: 2n ** 1023n }),
                   'foo=' + 2n ** 1023n);
assert.strictEqual(qs.stringify([0n, 1n, 2n]),
                   '0=0&1=1&2=2');

assert.strictEqual(qs.stringify({ foo: 2n ** 1023n },
                                null,
                                null,
                                { encodeURIComponent: (c) => c }),
                   'foo=' + 2n ** 1023n);
assert.strictEqual(qs.stringify([0n, 1n, 2n],
                                null,
                                null,
                                { encodeURIComponent: (c) => c }),
                   '0=0&1=1&2=2');

// Invalid surrogate pair throws URIError
assert.throws(
  () => qs.stringify({ foo: '\udc00' }),
  {
    code: 'ERR_INVALID_URI',
    name: 'URIError',
    message: 'URI malformed'
  }
);

// Coerce numbers to string
assert.strictEqual(qs.stringify({ foo: 0 }), 'foo=0');
assert.strictEqual(qs.stringify({ foo: -0 }), 'foo=0');
assert.strictEqual(qs.stringify({ foo: 3 }), 'foo=3');
assert.strictEqual(qs.stringify({ foo: -72.42 }), 'foo=-72.42');
assert.strictEqual(qs.stringify({ foo: NaN }), 'foo=');
assert.strictEqual(qs.stringify({ foo: 1e21 }), 'foo=1e%2B21');
assert.strictEqual(qs.stringify({ foo: Infinity }), 'foo=');

// nested
{
  const f = qs.stringify({
    a: 'b',
    q: qs.stringify({
      x: 'y',
      y: 'z'
    })
  });
  assert.strictEqual(f, 'a=b&q=x%3Dy%26y%3Dz');
}

qs.parse(undefined); // Should not throw.

// nested in colon
{
  const f = qs.stringify({
    a: 'b',
    q: qs.stringify({
      x: 'y',
      y: 'z'
    }, ';', ':')
  }, ';', ':');
  assert.strictEqual(f, 'a:b;q:x%3Ay%3By%3Az');
}

// empty string
assert.strictEqual(qs.stringify(), '');
assert.strictEqual(qs.stringify(0), '');
assert.strictEqual(qs.stringify([]), '');
assert.strictEqual(qs.stringify(null), '');
assert.strictEqual(qs.stringify(true), '');

check(qs.parse(), {});

// empty sep
check(qs.parse('a', []), { a: '' });

// empty eq
check(qs.parse('a', null, []), { '': 'a' });

// Test limiting
assert.strictEqual(
  Object.keys(qs.parse('a=1&b=1&c=1', null, null, { maxKeys: 1 })).length,
  1);

// Test limiting with a case that starts from `&`
assert.strictEqual(
  Object.keys(qs.parse('&a', null, null, { maxKeys: 1 })).length,
  0);

// Test removing limit
{
  function testUnlimitedKeys() {
    const query = {};

    for (let i = 0; i < 2000; i++) query[i] = i;

    const url = qs.stringify(query);

    assert.strictEqual(
      Object.keys(qs.parse(url, null, null, { maxKeys: 0 })).length,
      2000);
  }

  testUnlimitedKeys();
}

{
  const b = qs.unescapeBuffer('%d3%f2Ug%1f6v%24%5e%98%cb' +
    '%0d%ac%a2%2f%9d%eb%d8%a2%e6');
  // <Buffer d3 f2 55 67 1f 36 76 24 5e 98 cb 0d ac a2 2f 9d eb d8 a2 e6>
  assert.strictEqual(b[0], 0xd3);
  assert.strictEqual(b[1], 0xf2);
  assert.strictEqual(b[2], 0x55);
  assert.strictEqual(b[3], 0x67);
  assert.strictEqual(b[4], 0x1f);
  assert.strictEqual(b[5], 0x36);
  assert.strictEqual(b[6], 0x76);
  assert.strictEqual(b[7], 0x24);
  assert.strictEqual(b[8], 0x5e);
  assert.strictEqual(b[9], 0x98);
  assert.strictEqual(b[10], 0xcb);
  assert.strictEqual(b[11], 0x0d);
  assert.strictEqual(b[12], 0xac);
  assert.strictEqual(b[13], 0xa2);
  assert.strictEqual(b[14], 0x2f);
  assert.strictEqual(b[15], 0x9d);
  assert.strictEqual(b[16], 0xeb);
  assert.strictEqual(b[17], 0xd8);
  assert.strictEqual(b[18], 0xa2);
  assert.strictEqual(b[19], 0xe6);
}

assert.strictEqual(qs.unescapeBuffer('a+b', true).toString(), 'a b');
assert.strictEqual(qs.unescapeBuffer('a+b').toString(), 'a+b');
assert.strictEqual(qs.unescapeBuffer('a%').toString(), 'a%');
assert.strictEqual(qs.unescapeBuffer('a%2').toString(), 'a%2');
assert.strictEqual(qs.unescapeBuffer('a%20').toString(), 'a ');
assert.strictEqual(qs.unescapeBuffer('a%2g').toString(), 'a%2g');
assert.strictEqual(qs.unescapeBuffer('a%%').toString(), 'a%%');

// Test invalid encoded string
check(qs.parse('%\u0100=%\u0101'), { '%Ā': '%ā' });

// Test custom decode
{
  function demoDecode(str) {
    return str + str;
  }

  check(
    qs.parse('a=a&b=b&c=c', null, null, { decodeURIComponent: demoDecode }),
    { aa: 'aa', bb: 'bb', cc: 'cc' });
  check(
    qs.parse('a=a&b=b&c=c', null, '==', { decodeURIComponent: (str) => str }),
    { 'a=a': '', 'b=b': '', 'c=c': '' });
}

// Test QueryString.unescape
{
  function errDecode(str) {
    throw new Error('To jump to the catch scope');
  }

  check(qs.parse('a=a', null, null, { decodeURIComponent: errDecode }),
        { a: 'a' });
}

// Test custom encode
{
  function demoEncode(str) {
    return str[0];
  }

  const obj = { aa: 'aa', bb: 'bb', cc: 'cc' };
  assert.strictEqual(
    qs.stringify(obj, null, null, { encodeURIComponent: demoEncode }),
    'a=a&b=b&c=c');
}

// Test custom encode for different types
{
  const obj = { number: 1, bigint: 2n, true: true, false: false, object: {} };
  assert.strictEqual(
    qs.stringify(obj, null, null, { encodeURIComponent: (v) => v }),
    'number=1&bigint=2&true=true&false=false&object=');
}

// Test QueryString.unescapeBuffer
qsUnescapeTestCases.forEach((testCase) => {
  assert.strictEqual(qs.unescape(testCase[0]), testCase[1]);
  assert.strictEqual(qs.unescapeBuffer(testCase[0]).toString(), testCase[1]);
});

// Test overriding .unescape
{
  const prevUnescape = qs.unescape;
  qs.unescape = (str) => {
    return str.replace(/o/g, '_');
  };
  check(
    qs.parse('foo=bor'),
    createWithNoPrototype([{ key: 'f__', value: 'b_r' }]));
  qs.unescape = prevUnescape;
}
// Test separator and "equals" parsing order
check(qs.parse('foo&bar', '&', '&'), { foo: '', bar: '' });
