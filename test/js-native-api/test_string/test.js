'use strict';
const common = require('../../common');
const assert = require('assert');

// Testing api calls for string
const test_string = require(`./build/${common.buildType}/test_string`);

const empty = '';
assert.strictEqual(test_string.TestLatin1(empty), empty);
assert.strictEqual(test_string.TestUtf8(empty), empty);
assert.strictEqual(test_string.TestUtf16(empty), empty);
assert.strictEqual(test_string.TestLatin1AutoLength(empty), empty);
assert.strictEqual(test_string.TestUtf8AutoLength(empty), empty);
assert.strictEqual(test_string.TestUtf16AutoLength(empty), empty);
assert.strictEqual(test_string.TestLatin1External(empty), empty);
assert.strictEqual(test_string.TestUtf16External(empty), empty);
assert.strictEqual(test_string.TestLatin1ExternalAutoLength(empty), empty);
assert.strictEqual(test_string.TestUtf16ExternalAutoLength(empty), empty);
assert.strictEqual(test_string.TestPropertyKeyUtf16(empty), empty);
assert.strictEqual(test_string.TestPropertyKeyUtf16AutoLength(empty), empty);
assert.strictEqual(test_string.Utf16Length(empty), 0);
assert.strictEqual(test_string.Utf8Length(empty), 0);

const str1 = 'hello world';
assert.strictEqual(test_string.TestLatin1(str1), str1);
assert.strictEqual(test_string.TestUtf8(str1), str1);
assert.strictEqual(test_string.TestUtf16(str1), str1);
assert.strictEqual(test_string.TestLatin1AutoLength(str1), str1);
assert.strictEqual(test_string.TestUtf8AutoLength(str1), str1);
assert.strictEqual(test_string.TestUtf16AutoLength(str1), str1);
assert.strictEqual(test_string.TestLatin1External(str1), str1);
assert.strictEqual(test_string.TestUtf16External(str1), str1);
assert.strictEqual(test_string.TestLatin1ExternalAutoLength(str1), str1);
assert.strictEqual(test_string.TestUtf16ExternalAutoLength(str1), str1);
assert.strictEqual(test_string.TestLatin1Insufficient(str1), str1.slice(0, 3));
assert.strictEqual(test_string.TestUtf8Insufficient(str1), str1.slice(0, 3));
assert.strictEqual(test_string.TestUtf16Insufficient(str1), str1.slice(0, 3));
assert.strictEqual(test_string.TestPropertyKeyUtf16(str1), str1);
assert.strictEqual(test_string.TestPropertyKeyUtf16AutoLength(str1), str1);
assert.strictEqual(test_string.Utf16Length(str1), 11);
assert.strictEqual(test_string.Utf8Length(str1), 11);

const str2 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
assert.strictEqual(test_string.TestLatin1(str2), str2);
assert.strictEqual(test_string.TestUtf8(str2), str2);
assert.strictEqual(test_string.TestUtf16(str2), str2);
assert.strictEqual(test_string.TestLatin1AutoLength(str2), str2);
assert.strictEqual(test_string.TestUtf8AutoLength(str2), str2);
assert.strictEqual(test_string.TestUtf16AutoLength(str2), str2);
assert.strictEqual(test_string.TestLatin1External(str2), str2);
assert.strictEqual(test_string.TestUtf16External(str2), str2);
assert.strictEqual(test_string.TestLatin1ExternalAutoLength(str2), str2);
assert.strictEqual(test_string.TestUtf16ExternalAutoLength(str2), str2);
assert.strictEqual(test_string.TestLatin1Insufficient(str2), str2.slice(0, 3));
assert.strictEqual(test_string.TestUtf8Insufficient(str2), str2.slice(0, 3));
assert.strictEqual(test_string.TestUtf16Insufficient(str2), str2.slice(0, 3));
assert.strictEqual(test_string.TestPropertyKeyUtf16(str2), str2);
assert.strictEqual(test_string.TestPropertyKeyUtf16AutoLength(str2), str2);
assert.strictEqual(test_string.Utf16Length(str2), 62);
assert.strictEqual(test_string.Utf8Length(str2), 62);

const str3 = '?!@#$%^&*()_+-=[]{}/.,<>\'"\\';
assert.strictEqual(test_string.TestLatin1(str3), str3);
assert.strictEqual(test_string.TestUtf8(str3), str3);
assert.strictEqual(test_string.TestUtf16(str3), str3);
assert.strictEqual(test_string.TestLatin1AutoLength(str3), str3);
assert.strictEqual(test_string.TestUtf8AutoLength(str3), str3);
assert.strictEqual(test_string.TestUtf16AutoLength(str3), str3);
assert.strictEqual(test_string.TestLatin1External(str3), str3);
assert.strictEqual(test_string.TestUtf16External(str3), str3);
assert.strictEqual(test_string.TestLatin1ExternalAutoLength(str3), str3);
assert.strictEqual(test_string.TestUtf16ExternalAutoLength(str3), str3);
assert.strictEqual(test_string.TestLatin1Insufficient(str3), str3.slice(0, 3));
assert.strictEqual(test_string.TestUtf8Insufficient(str3), str3.slice(0, 3));
assert.strictEqual(test_string.TestUtf16Insufficient(str3), str3.slice(0, 3));
assert.strictEqual(test_string.TestPropertyKeyUtf16(str3), str3);
assert.strictEqual(test_string.TestPropertyKeyUtf16AutoLength(str3), str3);
assert.strictEqual(test_string.Utf16Length(str3), 27);
assert.strictEqual(test_string.Utf8Length(str3), 27);

const str4 = '¡¢£¤¥¦§¨©ª«¬­®¯°±²³´µ¶·¸¹º»¼½¾¿';
assert.strictEqual(test_string.TestLatin1(str4), str4);
assert.strictEqual(test_string.TestUtf8(str4), str4);
assert.strictEqual(test_string.TestUtf16(str4), str4);
assert.strictEqual(test_string.TestLatin1AutoLength(str4), str4);
assert.strictEqual(test_string.TestUtf8AutoLength(str4), str4);
assert.strictEqual(test_string.TestUtf16AutoLength(str4), str4);
assert.strictEqual(test_string.TestLatin1External(str4), str4);
assert.strictEqual(test_string.TestUtf16External(str4), str4);
assert.strictEqual(test_string.TestLatin1ExternalAutoLength(str4), str4);
assert.strictEqual(test_string.TestUtf16ExternalAutoLength(str4), str4);
assert.strictEqual(test_string.TestLatin1Insufficient(str4), str4.slice(0, 3));
assert.strictEqual(test_string.TestUtf8Insufficient(str4), str4.slice(0, 1));
assert.strictEqual(test_string.TestUtf16Insufficient(str4), str4.slice(0, 3));
assert.strictEqual(test_string.TestPropertyKeyUtf16(str4), str4);
assert.strictEqual(test_string.TestPropertyKeyUtf16AutoLength(str4), str4);
assert.strictEqual(test_string.Utf16Length(str4), 31);
assert.strictEqual(test_string.Utf8Length(str4), 62);

const str5 = 'ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþ';
assert.strictEqual(test_string.TestLatin1(str5), str5);
assert.strictEqual(test_string.TestUtf8(str5), str5);
assert.strictEqual(test_string.TestUtf16(str5), str5);
assert.strictEqual(test_string.TestLatin1AutoLength(str5), str5);
assert.strictEqual(test_string.TestUtf8AutoLength(str5), str5);
assert.strictEqual(test_string.TestUtf16AutoLength(str5), str5);
assert.strictEqual(test_string.TestLatin1External(str5), str5);
assert.strictEqual(test_string.TestUtf16External(str5), str5);
assert.strictEqual(test_string.TestLatin1ExternalAutoLength(str5), str5);
assert.strictEqual(test_string.TestUtf16ExternalAutoLength(str5), str5);
assert.strictEqual(test_string.TestLatin1Insufficient(str5), str5.slice(0, 3));
assert.strictEqual(test_string.TestUtf8Insufficient(str5), str5.slice(0, 1));
assert.strictEqual(test_string.TestUtf16Insufficient(str5), str5.slice(0, 3));
assert.strictEqual(test_string.TestPropertyKeyUtf16(str5), str5);
assert.strictEqual(test_string.TestPropertyKeyUtf16AutoLength(str5), str5);
assert.strictEqual(test_string.Utf16Length(str5), 63);
assert.strictEqual(test_string.Utf8Length(str5), 126);

const str6 = '\u{2003}\u{2101}\u{2001}\u{202}\u{2011}';
assert.strictEqual(test_string.TestUtf8(str6), str6);
assert.strictEqual(test_string.TestUtf16(str6), str6);
assert.strictEqual(test_string.TestUtf8AutoLength(str6), str6);
assert.strictEqual(test_string.TestUtf16AutoLength(str6), str6);
assert.strictEqual(test_string.TestUtf16External(str6), str6);
assert.strictEqual(test_string.TestUtf16ExternalAutoLength(str6), str6);
assert.strictEqual(test_string.TestUtf8Insufficient(str6), str6.slice(0, 1));
assert.strictEqual(test_string.TestUtf16Insufficient(str6), str6.slice(0, 3));
assert.strictEqual(test_string.TestPropertyKeyUtf16(str6), str6);
assert.strictEqual(test_string.TestPropertyKeyUtf16AutoLength(str6), str6);
assert.strictEqual(test_string.Utf16Length(str6), 5);
assert.strictEqual(test_string.Utf8Length(str6), 14);

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
