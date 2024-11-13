'use strict';
const common = require('../../common');
const assert = require('assert');

// Testing api calls for string
const test_string = require(`./build/${common.buildType}/test_string`);
// The insufficient buffer test case allocates a buffer of size 4, including
// the null terminator.
const kInsufficientIdx = 3;

const asciiCases = [
  '',
  'hello world',
  'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789',
  '?!@#$%^&*()_+-=[]{}/.,<>\'"\\',
];

const latin1Cases = [
  {
    str: '¡¢£¤¥¦§¨©ª«¬­®¯°±²³´µ¶·¸¹º»¼½¾¿',
    utf8Length: 62,
    utf8InsufficientIdx: 1,
  },
  {
    str: 'ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþ',
    utf8Length: 126,
    utf8InsufficientIdx: 1,
  },
];

const unicodeCases = [
  {
    str: '\u{2003}\u{2101}\u{2001}\u{202}\u{2011}',
    utf8Length: 14,
    utf8InsufficientIdx: 1,
  },
];

function testLatin1Cases(str) {
  assert.strictEqual(test_string.TestLatin1(str), str);
  assert.strictEqual(test_string.TestLatin1AutoLength(str), str);
  assert.strictEqual(test_string.TestLatin1External(str), str);
  assert.strictEqual(test_string.TestLatin1ExternalAutoLength(str), str);
  assert.strictEqual(test_string.TestPropertyKeyLatin1(str), str);
  assert.strictEqual(test_string.TestPropertyKeyLatin1AutoLength(str), str);
  assert.strictEqual(test_string.Latin1Length(str), str.length);

  if (str !== '') {
    assert.strictEqual(test_string.TestLatin1Insufficient(str), str.slice(0, kInsufficientIdx));
  }
}

function testUnicodeCases(str, utf8Length, utf8InsufficientIdx) {
  assert.strictEqual(test_string.TestUtf8(str), str);
  assert.strictEqual(test_string.TestUtf16(str), str);
  assert.strictEqual(test_string.TestUtf8AutoLength(str), str);
  assert.strictEqual(test_string.TestUtf16AutoLength(str), str);
  assert.strictEqual(test_string.TestUtf16External(str), str);
  assert.strictEqual(test_string.TestUtf16ExternalAutoLength(str), str);
  assert.strictEqual(test_string.TestPropertyKeyUtf8(str), str);
  assert.strictEqual(test_string.TestPropertyKeyUtf8AutoLength(str), str);
  assert.strictEqual(test_string.TestPropertyKeyUtf16(str), str);
  assert.strictEqual(test_string.TestPropertyKeyUtf16AutoLength(str), str);
  assert.strictEqual(test_string.Utf8Length(str), utf8Length);
  assert.strictEqual(test_string.Utf16Length(str), str.length);

  if (str !== '') {
    assert.strictEqual(test_string.TestUtf8Insufficient(str), str.slice(0, utf8InsufficientIdx));
    assert.strictEqual(test_string.TestUtf16Insufficient(str), str.slice(0, kInsufficientIdx));
  }
}

asciiCases.forEach(testLatin1Cases);
asciiCases.forEach((str) => testUnicodeCases(str, str.length, kInsufficientIdx));
latin1Cases.forEach((it) => testLatin1Cases(it.str));
latin1Cases.forEach((it) => testUnicodeCases(it.str, it.utf8Length, it.utf8InsufficientIdx));
unicodeCases.forEach((it) => testUnicodeCases(it.str, it.utf8Length, it.utf8InsufficientIdx));

assert.throws(() => {
  test_string.TestLargeUtf8();
}, /^Error: Invalid argument$/);

assert.throws(() => {
  test_string.TestLargeLatin1();
}, /^Error: Invalid argument$/);

assert.throws(() => {
  test_string.TestLargeUtf16();
}, /^Error: Invalid argument$/);

test_string.TestMemoryCorruption(' '.repeat(64 * 1024));
