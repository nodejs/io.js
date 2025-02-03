'use strict';
const common = require('../common');
const assert = require('assert');

const b = Buffer.from('abcdef');
const buf_a = Buffer.from('a');
const buf_bc = Buffer.from('bc');
const buf_f = Buffer.from('f');
const buf_z = Buffer.from('z');
const buf_empty = Buffer.from('');

assert(b.includes('a'));
assert(!b.includes('a', 1));
assert(!b.includes('a', -1));
assert(!b.includes('a', -4));
assert(b.includes('a', -b.length));
assert(b.includes('a', NaN));
assert(b.includes('a', -Infinity));
assert(!b.includes('a', Infinity));
assert(b.includes('bc'));
assert(!b.includes('bc', 2));
assert(!b.includes('bc', -1));
assert(!b.includes('bc', -3));
assert(b.includes('bc', -5));
assert(b.includes('bc', NaN));
assert(b.includes('bc', -Infinity));
assert(!b.includes('bc', Infinity));
assert(b.includes('f'), b.length - 1);
assert(!b.includes('z'));
assert(b.includes(''));
assert(b.includes('', 1));
assert(b.includes('', b.length + 1));
assert(b.includes('', Infinity));
assert(b.includes(buf_a));
assert(!b.includes(buf_a, 1));
assert(!b.includes(buf_a, -1));
assert(!b.includes(buf_a, -4));
assert(b.includes(buf_a, -b.length));
assert(b.includes(buf_a, NaN));
assert(b.includes(buf_a, -Infinity));
assert(!b.includes(buf_a, Infinity));
assert(b.includes(buf_bc));
assert(!b.includes(buf_bc, 2));
assert(!b.includes(buf_bc, -1));
assert(!b.includes(buf_bc, -3));
assert(b.includes(buf_bc, -5));
assert(b.includes(buf_bc, NaN));
assert(b.includes(buf_bc, -Infinity));
assert(!b.includes(buf_bc, Infinity));
assert(b.includes(buf_f), b.length - 1);
assert(!b.includes(buf_z));
assert(b.includes(buf_empty));
assert(b.includes(buf_empty, 1));
assert(b.includes(buf_empty, b.length + 1));
assert(b.includes(buf_empty, Infinity));
assert(b.includes(0x61));
assert(!b.includes(0x61, 1));
assert(!b.includes(0x61, -1));
assert(!b.includes(0x61, -4));
assert(b.includes(0x61, -b.length));
assert(b.includes(0x61, NaN));
assert(b.includes(0x61, -Infinity));
assert(!b.includes(0x61, Infinity));
assert(!b.includes(0x0));

// test offsets
assert(b.includes('d', 2));
assert(b.includes('f', 5));
assert(b.includes('f', -1));
assert(!b.includes('f', 6));

assert(b.includes(Buffer.from('d'), 2));
assert(b.includes(Buffer.from('f'), 5));
assert(b.includes(Buffer.from('f'), -1));
assert(!b.includes(Buffer.from('f'), 6));

assert(!Buffer.from('ff').includes(Buffer.from('f'), 1, 'ucs2'));

// test hex encoding
assert.strictEqual(
  Buffer.from(b.toString('hex'), 'hex')
    .includes('64', 0, 'hex'),
  true
);
assert.strictEqual(
  Buffer.from(b.toString('hex'), 'hex')
    .includes(Buffer.from('64', 'hex'), 0, 'hex'),
  true
);

// Test base64 encoding
assert.strictEqual(
  Buffer.from(b.toString('base64'), 'base64')
    .includes('ZA==', 0, 'base64'),
  true
);
assert.strictEqual(
  Buffer.from(b.toString('base64'), 'base64')
    .includes(Buffer.from('ZA==', 'base64'), 0, 'base64'),
  true
);

// test ascii encoding
assert.strictEqual(
  Buffer.from(b.toString('ascii'), 'ascii')
    .includes('d', 0, 'ascii'),
  true
);
assert.strictEqual(
  Buffer.from(b.toString('ascii'), 'ascii')
    .includes(Buffer.from('d', 'ascii'), 0, 'ascii'),
  true
);

// Test latin1 encoding
assert.strictEqual(
  Buffer.from(b.toString('latin1'), 'latin1')
    .includes('d', 0, 'latin1'),
  true
);
assert.strictEqual(
  Buffer.from(b.toString('latin1'), 'latin1')
    .includes(Buffer.from('d', 'latin1'), 0, 'latin1'),
  true
);

// Test binary encoding
assert.strictEqual(
  Buffer.from(b.toString('binary'), 'binary')
    .includes('d', 0, 'binary'),
  true
);
assert.strictEqual(
  Buffer.from(b.toString('binary'), 'binary')
    .includes(Buffer.from('d', 'binary'), 0, 'binary'),
  true
);


// test ucs2 encoding
let twoByteString = Buffer.from('\u039a\u0391\u03a3\u03a3\u0395', 'ucs2');

assert(twoByteString.includes('\u0395', 4, 'ucs2'));
assert(twoByteString.includes('\u03a3', -4, 'ucs2'));
assert(twoByteString.includes('\u03a3', -6, 'ucs2'));
assert(twoByteString.includes(
  Buffer.from('\u03a3', 'ucs2'), -6, 'ucs2'));
assert(!twoByteString.includes('\u03a3', -2, 'ucs2'));

const mixedByteStringUcs2 =
  Buffer.from('\u039a\u0391abc\u03a3\u03a3\u0395', 'ucs2');
assert(mixedByteStringUcs2.includes('bc', 0, 'ucs2'));
assert(mixedByteStringUcs2.includes('\u03a3', 0, 'ucs2'));
assert(!mixedByteStringUcs2.includes('\u0396', 0, 'ucs2'));

assert.ok(
  mixedByteStringUcs2.includes(Buffer.from('bc', 'ucs2'), 0, 'ucs2'));
assert.ok(
  mixedByteStringUcs2.includes(Buffer.from('\u03a3', 'ucs2'), 0, 'ucs2'));
assert.ok(
  !mixedByteStringUcs2.includes(Buffer.from('\u0396', 'ucs2'), 0, 'ucs2'));

twoByteString = Buffer.from('\u039a\u0391\u03a3\u03a3\u0395', 'ucs2');

// Test single char pattern
assert(twoByteString.includes('\u039a', 0, 'ucs2'));
assert(twoByteString.includes('\u0391', 0, 'ucs2'), 'Alpha');
assert(twoByteString.includes('\u03a3', 0, 'ucs2'), 'First Sigma');
assert(twoByteString.includes('\u03a3', 6, 'ucs2'), 'Second Sigma');
assert(twoByteString.includes('\u0395', 0, 'ucs2'), 'Epsilon');
assert(!twoByteString.includes('\u0392', 0, 'ucs2'), 'Not beta');

// Test multi-char pattern
assert(twoByteString.includes('\u039a\u0391', 0, 'ucs2'), 'Lambda Alpha');
assert(twoByteString.includes('\u0391\u03a3', 0, 'ucs2'), 'Alpha Sigma');
assert(twoByteString.includes('\u03a3\u03a3', 0, 'ucs2'), 'Sigma Sigma');
assert(twoByteString.includes('\u03a3\u0395', 0, 'ucs2'), 'Sigma Epsilon');

const mixedByteStringUtf8 = Buffer.from('\u039a\u0391abc\u03a3\u03a3\u0395');
assert(mixedByteStringUtf8.includes('bc'));
assert(mixedByteStringUtf8.includes('bc', 5));
assert(mixedByteStringUtf8.includes('bc', -8));
assert(mixedByteStringUtf8.includes('\u03a3'));
assert(!mixedByteStringUtf8.includes('\u0396'));


// Test complex string includes algorithms. Only trigger for long strings.
// Long string that isn't a simple repeat of a shorter string.
let longString = 'A';
for (let i = 66; i < 76; i++) {  // from 'B' to 'K'
  longString = longString + String.fromCharCode(i) + longString;
}

const longBufferString = Buffer.from(longString);

// Pattern of 15 chars, repeated every 16 chars in long
let pattern = 'ABACABADABACABA';
for (let i = 0; i < longBufferString.length - pattern.length; i += 7) {
  const includes = longBufferString.includes(pattern, i);
  assert(includes, `Long ABACABA...-string at index ${i}`);
}
assert(longBufferString.includes('AJABACA'), 'Long AJABACA, First J');
assert(longBufferString.includes('AJABACA', 511), 'Long AJABACA, Second J');

pattern = 'JABACABADABACABA';
assert(longBufferString.includes(pattern), 'Long JABACABA..., First J');
assert(longBufferString.includes(pattern, 512), 'Long JABACABA..., Second J');

// Search for a non-ASCII string in a pure ASCII string.
const asciiString = Buffer.from(
  'arglebargleglopglyfarglebargleglopglyfarglebargleglopglyf');
assert(!asciiString.includes('\x2061'));
assert(asciiString.includes('leb', 0));

// Search in string containing many non-ASCII chars.
const allCharsString = Array.from({ length: 65536 }, (_, i) => String.fromCharCode(i)).join('');
const allCharsBufferUtf8 = Buffer.from(allCharsString);
const allCharsBufferUcs2 = Buffer.from(allCharsString, 'ucs2');

// Search for string long enough to trigger complex search with ASCII pattern
// and UC16 subject.
assert(!allCharsBufferUtf8.includes('notfound'));
assert(!allCharsBufferUcs2.includes('notfound'));

// Find substrings in Utf8.
let lengths = [1, 3, 15];  // Single char, simple and complex.
let indices = [0x5, 0x60, 0x400, 0x680, 0x7ee, 0xFF02, 0x16610, 0x2f77b];
for (let lengthIndex = 0; lengthIndex < lengths.length; lengthIndex++) {
  for (let i = 0; i < indices.length; i++) {
    const index = indices[i];
    let length = lengths[lengthIndex];

    if (index + length > 0x7F) {
      length = 2 * length;
    }

    if (index + length > 0x7FF) {
      length = 3 * length;
    }

    if (index + length > 0xFFFF) {
      length = 4 * length;
    }

    const patternBufferUtf8 = allCharsBufferUtf8.slice(index, index + length);
    assert(index, allCharsBufferUtf8.includes(patternBufferUtf8));

    const patternStringUtf8 = patternBufferUtf8.toString();
    assert(index, allCharsBufferUtf8.includes(patternStringUtf8));
  }
}

// Find substrings in Usc2.
lengths = [2, 4, 16];  // Single char, simple and complex.
indices = [0x5, 0x65, 0x105, 0x205, 0x285, 0x2005, 0x2085, 0xfff0];
for (let lengthIndex = 0; lengthIndex < lengths.length; lengthIndex++) {
  for (let i = 0; i < indices.length; i++) {
    const index = indices[i] * 2;
    const length = lengths[lengthIndex];

    const patternBufferUcs2 =
      allCharsBufferUcs2.slice(index, index + length);
    assert.ok(
      allCharsBufferUcs2.includes(patternBufferUcs2, 0, 'ucs2'));

    const patternStringUcs2 = patternBufferUcs2.toString('ucs2');
    assert.ok(
      allCharsBufferUcs2.includes(patternStringUcs2, 0, 'ucs2'));
  }
}

[
  () => { },
  {},
  [],
].forEach((val) => {
  assert.throws(
    () => b.includes(val),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: 'The "value" argument must be one of type number or string ' +
               'or an instance of Buffer or Uint8Array.' +
               common.invalidArgTypeHelper(val)
    }
  );
});

// Test truncation of Number arguments to uint8
{
  const buf = Buffer.from('this is a test');
  assert.ok(buf.includes(0x6973));
  assert.ok(buf.includes(0x697320));
  assert.ok(buf.includes(0x69732069));
  assert.ok(buf.includes(0x697374657374));
  assert.ok(buf.includes(0x69737374));
  assert.ok(buf.includes(0x69737465));
  assert.ok(buf.includes(0x69737465));
  assert.ok(buf.includes(-140));
  assert.ok(buf.includes(-152));
  assert.ok(!buf.includes(0xff));
  assert.ok(!buf.includes(0xffff));
}
