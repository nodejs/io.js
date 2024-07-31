// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-regexp-duplicate-named-groups

// Duplicate names are only valid in alterations. Test that previous behaviour
// is still correct (early syntax error).
assertEarlyError('/(?<a>.)(?<a>.)/');
assertEarlyError('/((?<a>.)(?<a>.))/');
assertEarlyError('/(?<a>.)((?<a>.))/');
assertEarlyError('/((?<a>.))(?<a>.)/');
assertEarlyError('/(?<a>.)((?<a>.)|(?<b>.))/');
assertEarlyError('/(?<b>.)((?<a>.)|(?<b>.))/');
assertEarlyError('/((?<a>.)|(?<b>.))(?<a>.)/');
assertEarlyError('/((?<a>.)|(?<b>.))(?<b>.)/');
assertEarlyError('/((?<a>.)|((?<b>.)|(?<c>.))(?<b>.)/');
assertEarlyError('/((?<a>.)|((?<b>.)|(?<c>.))(?<c>.)/');
assertEarlyError(
    '/x(?<a>.)((((?<a>.)|(?<a>.))|(?<a>.)|(?<a>.))|(?<a>.))|(?<a>.)y/');
assertEarlyError(
    '/x((?<a>.)(((?<a>.)|(?<a>.))|(?<a>.)|(?<a>.))|(?<a>.))|(?<a>.)y/');
assertEarlyError(
    '/x(((?<a>.)((?<a>.)|(?<a>.))|(?<a>.)|(?<a>.))|(?<a>.))|(?<a>.)y/');
assertEarlyError(
    '/x((((?<a>.)(?<a>.)|(?<a>.))|(?<a>.)|(?<a>.))|(?<a>.))|(?<a>.)y/');
assertEarlyError(
    '/x(?<a>.)|((?<a>.)|(?<a>.)|((?<a>.)|((?<a>.)|(?<a>.)(?<a>.))))y/');
assertEarlyError(
    '/x(?<a>.)|((?<a>.)|(?<a>.)|((?<a>.)|((?<a>.)|(?<a>.))(?<a.)))y/');
assertEarlyError(
    '/x(?<a>.)|((?<a>.)|(?<a>.)|((?<a>.)|((?<a>.)|(?<a>.)))(?<a>.))y/');
assertEarlyError(
    '/x(?<a>.)|((?<a>.)|(?<a>.)|((?<a>.)|((?<a>.)|(?<a>.))))(?<a>.)y/');

// Cases with valid duplicate names.
new RegExp('(?<a>.)|(?<a>.)');
new RegExp('(?<a>.)|(?<a>.)|(?<a>.)|(?<a>.)');
new RegExp('(?<a>.)|(.)|(?<a>.)');
new RegExp('(?<a>.(?<b>.))|(?<a>.)');
new RegExp('(?<a>.(?<b>.))|(?<a>.)|(?<b>.)');
new RegExp('((?<a>.)|((?<b>.)|(?<c>.))(?<a>.))');
new RegExp('((?<a>.)|(?<b>.))|(?<c>.)(?<a>.)');
new RegExp('((?<a>.)|(?<b>.))|(?<c>.)(?<b>.)');
new RegExp('(?:(?<a>.)|(?<a>.))|(?<a>.)');
new RegExp('(?<a>.)|(?:(?<a>.)|(?<a>.))');
new RegExp('x((((?<a>.)|(?<a>.))|(?<a>.)|(?<a>.))|(?<a>.))|(?<a>.)y');
new RegExp('x(?<a>.)|((?<a>.)|(?<a>.)|((?<a>.)|((?<a>.)|(?<a>.))))y');

// Test different functions with duplicate names in alteration.
assertEquals(
    ['xxyy', undefined, 'y'], /(?:(?:(?<a>x)|(?<a>y))\k<a>){2}/.exec('xxyy'));
assertEquals(
    ['zzyyxx', 'x', undefined, undefined, undefined, undefined],
    /(?:(?:(?<a>x)|(?<a>y)|(a)|(?<b>b)|(?<a>z))\k<a>){3}/.exec('xzzyyxxy'));
assertEquals(
    ['xxyy', undefined, 'y'], 'xxyy'.match(/(?:(?:(?<a>x)|(?<a>y))\k<a>){2}/));
assertEquals(
    ['zzyyxx', 'x', undefined, undefined, undefined, undefined],
    'xzzyyxxy'.match(/(?:(?:(?<a>x)|(?<a>y)|(a)|(?<b>b)|(?<a>z))\k<a>){3}/));
assertTrue(/(?:(?:(?<a>x)|(?<a>y))\k<a>){2}/.test('xxyy'));
assertTrue(
    /(?:(?:(?<a>x)|(?<a>y)|(a)|(?<b>b)|(?<a>z))\k<a>){3}/.test('xzzyyxxy'));
assertFalse(/(?:(?:(?<a>x)|(?<a>y))\k<a>){2}/.test('xyxy'));
assertFalse(
    /(?:(?:(?<a>x)|(?<a>y)|(a)|(?<b>b)|(?<a>z))\k<a>){3}/.test('xyzxyzxyz'));
assertEquals(3, 'abcxyz'.search(/(?<a>x)|(?<a>y)/));
assertEquals(3, 'abcxyz'.search(/(?<a>y)|(?<a>x)/));
assertEquals(1, 'aybcxyz'.search(/(?<a>x)|(?<a>y)/));
assertEquals(1, 'aybcxyz'.search(/(?<a>y)|(?<a>x)/));
assertEquals('2xyy', 'xxyy'.replace(/(?:(?:(?<a>x)|(?<a>y))\k<a>)/, '2$<a>'));
assertEquals(
    'x2zyyxxy',
    'xzzyyxxy'.replace(
        /(?:(?:(?<a>x)|(?<a>y)|(a)|(?<b>b)|(?<a>z))\k<a>)/, '2$<a>'));
assertEquals(
    '2x(x,)yy', 'xxyy'.replace(/(?:(?:(?<a>x)|(?<a>y))\k<a>)/, '2$<a>($1,$2)'));
assertEquals(
    'x2z(,,,,z)yyxxy',
    'xzzyyxxy'.replace(
        /(?:(?:(?<a>x)|(?<a>y)|(a)|(?<b>b)|(?<a>z))\k<a>)/,
        '2$<a>($1,$2,$3,$4,$5)'));
assertEquals('2x2y', 'xxyy'.replace(/(?:(?:(?<a>x)|(?<a>y))\k<a>)/g, '2$<a>'));
assertEquals(
    'x2z2y2xy',
    'xzzyyxxy'.replace(
        /(?:(?:(?<a>x)|(?<a>y)|(a)|(?<b>b)|(?<a>z))\k<a>)/g, '2$<a>'));
assertEquals(
    '2x&2y', 'xx&yy'.replaceAll(/(?:(?:(?<a>x)|(?<a>y))\k<a>)/g, '2$<a>'));
assertEquals(
    'x&2z&2y&2x&y',
    'x&zz&yy&xx&y'.replaceAll(
        /(?:(?:(?<a>x)|(?<a>y)|(a)|(?<b>b)|(?<a>z))\k<a>)/g, '2$<a>'));
assertEquals(
    ['', 'x', undefined, '', undefined, 'y', ''],
    'xxyy'.split(/(?:(?:(?<a>x)|(?<a>y))\k<a>)/));
assertEquals(
    [
      'x', undefined, undefined, undefined, undefined, 'z', '', undefined, 'y',
      undefined, undefined, undefined, '', 'x', undefined, undefined, undefined,
      undefined, 'y'
    ],
    'xzzyyxxy'.split(/(?:(?:(?<a>x)|(?<a>y)|(a)|(?<b>b)|(?<a>z))\k<a>)/));

function assertMatchAll(matches, expected) {
  let i = 0;
  for (match of matches) {
    assertEquals(expected[i], match);
    i++;
  }
}
assertMatchAll(
    'xyx'.matchAll(/(?<a>x)|(?<a>y)/g),
    [['x', 'x', undefined], ['y', undefined, 'y'], ['x', 'x', undefined]]);
assertMatchAll(
    'xyx'.matchAll(/(?<a>y)|(?<a>x)/g),
    [['x', undefined, 'x'], ['y', 'y', undefined], ['x', undefined, 'x']]);

// Property enumeration order of the groups object is based on source order, not
// match order.
assertEquals(
    ['b', 'a'], Object.keys(/(?<b>x)(?<a>x)|(?<a>y)(?<b>y)/.exec('xx').groups));
assertEquals(
    ['b', 'a'], Object.keys(/(?<b>x)(?<a>x)|(?<a>y)(?<b>y)/.exec('yy').groups));

// Test match indices with duplicate groups.
assertEquals([2, 3], 'abxy'.match(/(?<a>x)|(?<a>y)/d).indices.groups.a);
assertEquals([2, 3], 'bayx'.match(/(?<a>x)|(?<a>y)/d).indices.groups.a);

// Replace with function as replacement.
function testReplaceWithCallback(global) {
  let replace_callback_cnt = 0;

  function checkReplace(match, c1, c2, offset, string, groups) {
    replace_callback_cnt++;
    if (offset == 0) {
      // First callback for match 'xx'.
      assertEquals('xx', match);
      assertEquals('x', c1);
      assertEquals(undefined, c2);
      assertEquals('xxyy', string);
    } else {
      // Second callback for match 'yy'.
      assertTrue(global);
      assertEquals(2, offset);
      assertEquals('yy', match);
      assertEquals(undefined, c1);
      assertEquals('y', c2);
      assertEquals('xxyy', string);
    }
    return '2' + groups.a;
  }

  let re = new RegExp('(?:(?:(?<a>x)|(?<a>y))\\k<a>)', global ? 'g' : '');
  let expected = global ? '2x2y' : '2xyy';
  assertEquals(expected, 'xxyy'.replace(re, checkReplace));
  assertEquals(global ? 2 : 1, replace_callback_cnt);
}

testReplaceWithCallback(false);
testReplaceWithCallback(true);
