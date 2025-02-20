// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// u and v are not allowed together.
assertEarlyError('/./uv');
assertThrowsAtRuntime("new RegExp('.','uv')", SyntaxError);

assertEquals('v', /./v.flags);
assertTrue(/./v.unicodeSets);

// Characters that require escaping within a character class in /v mode
assertEarlyError('/[(]/v');
assertEarlyError('/[)]/v');
assertEarlyError('/[[]/v');
assertEarlyError('/[]]/v');
assertEarlyError('/[{]/v');
assertEarlyError('/[}]/v');
assertEarlyError('/[/]/v');
assertEarlyError('/[-]/v');
// Need to escape the backslash, as assertEarlyError uses eval().
assertEarlyError('/[\\]/v');
assertEarlyError('/[|]/v');

assertEarlyError('/[&&]/v');
assertEarlyError('/[!!]/v');
assertEarlyError('/[##]/v');
assertEarlyError('/[$$]/v');
assertEarlyError('/[%%]/v');
assertEarlyError('/[**]/v');
assertEarlyError('/[++]/v');
assertEarlyError('/[,,]/v');
assertEarlyError('/[..]/v');
assertEarlyError('/[::]/v');
assertEarlyError('/[;;]/v');
assertEarlyError('/[<<]/v');
assertEarlyError('/[==]/v');
assertEarlyError('/[>>]/v');
assertEarlyError('/[??]/v');
assertEarlyError('/[@@]/v');
// The first ^ negates the class. The following two are not valid.
assertEarlyError('/[^^^]/v');
assertEarlyError('/[``]/v');
assertEarlyError('/[~~]/v');

assertEarlyError('/[a&&&]/v');
assertEarlyError('/[&&&a]/v');

// Unterminated string disjunction.
assertEarlyError('/[\\q{foo]/v');
assertEarlyError('/[\\q{foo|]/v');

// Negating classes containing strings is not allowed.
assertEarlyError('/[^\\q{foo}]/v');
assertEarlyError('/[^\\q{}]/v');  // Empty string counts as string.
assertEarlyError('/[^[\\q{foo}]]/v');
assertEarlyError('/[^[\\p{Basic_Emoji}]/v');
assertEarlyError('/[^\\q{foo}&&\\q{bar}]/v');
assertEarlyError('/[^\\q{foo}--\\q{bar}]/v');
// Exceptions when negating the class is allowed:
// The "string" contains only single characters.
/[^\q{a|b|c}]/v;
// Not all operands of an intersection contain strings.
/[^\q{foo}&&\q{bar}&&a]/v;
// The first operand of a subtraction doesn't contain strings.
/[^a--\q{foo}--\q{bar}]/v;

// Negated properties of strings are not allowed.
assertEarlyError('/\\P{Basic_Emoji}/v');
assertEarlyError('/\\P{Emoji_Keycap_Sequence}/v');
assertEarlyError('/\\P{RGI_Emoji_Modifier_Sequence}/v');
assertEarlyError('/\\P{RGI_Emoji_Flag_Sequence}/v');
assertEarlyError('/\\P{RGI_Emoji_Tag_Sequence}/v');
assertEarlyError('/\\P{RGI_Emoji_ZWJ_Sequence}/v');
assertEarlyError('/\\P{RGI_Emoji}/v');

// Invalid identity escape in string disjunction.
assertEarlyError('/[\\q{\\w}]/v');

const allAscii = Array.from(
    {length: 127}, (v, i) => { return String.fromCharCode(i); });

function check(re, expectMatch, expectNoMatch = [], negationValid = true) {
  if (expectNoMatch === undefined) {
    const expectSet = new Set(expectMatch.map(val => {
      return (typeof val == 'number') ? String(val) : val; }));
    expectNoMatch = allAscii.filter(val => !expectSet.has(val));
  }
  for (const match of expectMatch) {
    assertTrue(re.test(match), `${re}.test(${match})`);
  }
  for (const noMatch of expectNoMatch) {
    assertFalse(re.test(noMatch), `${re}.test(${noMatch})`);
  }
  if (!negationValid) {
    // Negation of classes containing strings is an error.
    const negated = `[^${re.source}]`;
    assertThrows(() => { new RegExp(negated, `${re.flags}`); }, SyntaxError,
        `Invalid regular expression: /${negated}/${re.flags}: ` +
        `Negated character class may contain strings`);
  } else {
    // Nest the current RegExp in a negated class and check expectations are
    // inversed.
    const inverted = new RegExp(`[^${re.source}]`, re.flags);
    for (const match of expectMatch) {
      assertFalse(inverted.test(match), `${inverted}.test(${match})`);
    }
    for (const noMatch of expectNoMatch) {
      assertTrue(inverted.test(noMatch), `${inverted}.test(${noMatch})`);
    }
  }
}

// Union with nested class
check(
    /[\da-f[xy][^[^z]]]/v, Array.from('0123456789abcdefxyz'),
    Array.from('ghijklmnopqrstuv!?'));

// Intersections
check(/[\d&&[0-9]]/v, Array.from('0123456789'), []);
check(/[\d&&0]/v, [0], Array.from('123456789'));
check(/[\d&&9]/v, [9], Array.from('012345678'));
check(/[\d&&[02468]]/v, Array.from('02468'), Array.from('13579'));
check(/[\d&&[13579]]/v, Array.from('13579'), Array.from('02468'));
check(
    /[\w&&[^a-zA-Z_]]/v, Array.from('0123456789'),
    Array.from('abcdxyzABCDXYZ_!?'));
check(
    /[^\w&&[a-zA-Z_]]/v, Array.from('0123456789!?'),
    Array.from('abcdxyzABCDXYZ_'));

// Subtractions
check(/[\d--[!-%]]/v, Array.from('0123456789'));
check(/[\d--[A-Z]]/v, Array.from('0123456789'));
check(/[\d--[0-9]]/v, []);
check(/[\d--[\w]]/v, []);
check(/[\d--0]/v, Array.from('123456789'));
check(/[\d--9]/v, Array.from('012345678'));
check(/[[\d[a-c]]--9]/v, Array.from('012345678abc'));
check(/[\d--[02468]]/v, Array.from('13579'));
check(/[\d--[13579]]/v, Array.from('02468'));
check(/[[3-7]--[0-9]]/v, []);
check(/[[3-7]--[0-7]]/v, []);
check(/[[3-7]--[3-9]]/v, []);
check(/[[3-79]--[0-7]]/v, [9]);
check(/[[3-79]--[3-9]]/v, []);
check(/[[3-7]--[0-3]]/v, Array.from('4567'));
check(/[[3-7]--[0-5]]/v, Array.from('67'));
check(/[[3-7]--[7-9]]/v, Array.from('3456'));
check(/[[3-7]--[5-9]]/v, Array.from('34'));
check(/[[3-7a-c]--[0-3]]/v, Array.from('4567abc'));
check(/[[3-7a-c]--[0-5]]/v, Array.from('67abc'));
check(/[[3-7a-c]--[7-9]]/v, Array.from('3456abc'));
check(/[[3-7a-c]--[5-9]]/v, Array.from('34abc'));
check(/[[2-8]--[0-3]--5--[7-9]]/v, Array.from('46'));
check(/[[2-57-8]--[0-3]--[5-7]]/v, Array.from('48'));
check(/[[0-57-8]--[1-34]--[5-7]]/v, Array.from('08'));
check(/[\d--[^02468]]/v, Array.from('02468'));
check(/[\d--[^13579]]/v, Array.from('13579'));
check(/[[a-c]--\0]/v, Array.from('abc'));

// Ignore-Case
check(/[Ā-č]/v, Array.from('ĀāĂăĄąĆć'), Array.from('abc'));
check(/[ĀĂĄĆ]/vi, Array.from('ĀāĂăĄąĆć'), Array.from('abc'));
check(/[āăąć]/vi, Array.from('ĀāĂăĄąĆć'), Array.from('abc'));

// Ignore-Case intersections
check(
    /[\w&&[^a-z_]]/vi, Array.from('0123456789'),
    Array.from('abcdxyzABCDXYZ_!?'));
check(
    /[^\w&&[a-z_]]/vi, Array.from('0123456789!?'),
    Array.from('abcdxyzABCDXYZ_'));
check(/[K&&K]/vi, Array.from('KkK')); // K && U+212A (Kelvin Sign)
check(/[K&&K]/vi, Array.from('KkK')); // U+212A (Kelvin Sign) && K
check(/[K&&\u{212A}]/vi, Array.from('Kk\u{212A}'));
check(/[\u{212A}&&K]/vi, Array.from('Kk\u{212A}'));

// Ignore-Case subtractions
check(/[\d--[A-Z]]/vi, Array.from('0123456789'), Array.from('abcdEFGH'));
check(/[[\d[a-c]]--9]/vi, Array.from('012345678abcABC'), Array.from('9xX'));
check(/[K--K]/vi, [], Array.from('KkK')); // K -- U+212A (Kelvin Sign)
check(/[K--k]/vi, [], Array.from('KkK')); // U+212A (Kelvin Sign) -- k
check(/[K--\u{212A}]/vi, [], Array.from('KkK'));
check(/[\u{212A}--k]/vi, [], Array.from('KkK'));

// String disjunctions
check(/[\q{foo|bar|0|5}]/v, ['foo', 'bar', 0, 5], ['fo', 'baz'], false);
check(/[\q{foo|bar}[05]]/v, ['foo', 'bar', 0, 5], ['fo', 'baz'], false);
check(
    /[\q{foo|bar|0|5}&&\q{bar}]/v, ['bar'], ['foo', 0, 5, 'fo', 'baz'], false);
// The second operand of the intersection doesn't contain strings, so the result
// will not contain strings and therefore negation is valid.
check(/[\q{foo|bar|0|5}&&\d]/v, [0, 5], ['foo', 'bar', 'fo', 'baz'], true);
check(
    /[\q{foo|bar|0|5}--\q{foo}]/v, ['bar', 0, 5], ['foo', 'fo', 'baz'], false);
check(/[\q{foo|bar|0|5}--\d]/v, ['foo', 'bar'], [0, 5, 'fo', 'baz'], false);
check(
    /[\q{foo|bar|3|2|0}--\d]/v, ['foo', 'bar'], [0, 1, 2, 3, 4, 5, 'fo', 'baz'],
    false);

check(
    /[\q{foo|bar|0|5}&&\q{bAr}]/vi, ['bar', 'bAr', 'BAR'],
    ['foo', 0, 5, 'fo', 'baz'], false);
check(
    /[\q{foo|bar|0|5}--\q{FoO}]/vi, ['bar', 'bAr', 'BAR', 0, 5],
    ['foo', 'FOO', 'fo', 'baz'], false);

check(/[\q{ĀĂĄĆ|AaAc}&&\q{āăąć}]/vi, ['ĀĂĄĆ', 'āăąć'], ['AaAc'], false);
check(
    /[\q{ĀĂĄĆ|AaAc}--\q{āăąć}]/vi, ['AaAc', 'aAaC'], ['ĀĂĄĆ', 'āăąć'],
    false);

// Empty nested classes.
check(/[a-c\q{foo|bar}[]]/v, ['a','b','c','foo','bar'], [], false);
check(/[[a-c\q{foo|bar}]&&[]]/v, [], ['a','b','c','foo','bar'], true);
check(/[[a-c\q{foo|bar}]--[]]/v, ['a','b','c','foo','bar'], [], false);
check(/[[]&&[a-c\q{foo|bar}]]/v, [], ['a','b','c','foo','bar'], true);
check(/[[]--[a-c\q{foo|bar}]]/v, [], ['a','b','c','foo','bar'], true);

// Empty string disjunctions matches nothing, but succeeds.
let res = /[\q{}]/v.exec('foo');
assertNotNull(res);
assertEquals(1, res.length);
assertEquals('', res[0]);

// Ensure longest strings are matched first.
assertEquals(['xyz'], /[a-c\q{W|xy|xyz}]/v.exec('xyzabc'));
assertEquals(['xyz'], /[a-c\q{W|xyz|xy}]/v.exec('xyzabc'));
assertEquals(['xyz'], /[\q{W|xyz|xy}a-c]/v.exec('xyzabc'));
// Empty string is last.
assertEquals(['a'], /[\q{W|}a-c]/v.exec('abc'));

// Some more sophisticated tests taken from
// https://v8.dev/features/regexp-v-flag
assertTrue(/^\p{RGI_Emoji}$/v.test('⚽'));
assertTrue(/^\p{RGI_Emoji}$/v.test('👨🏾‍⚕️'));
assertFalse(/[\p{Script_Extensions=Greek}--π]/v.test('π'));
assertFalse(/[\p{Script_Extensions=Greek}--[αβγ]]/v.test('α'));
assertFalse(/[\p{Script_Extensions=Greek}--[α-γ]]/v.test('β'));
assertTrue(/[\p{Decimal_Number}--[0-9]]/v.test('𑜹'));
assertFalse(/[\p{Decimal_Number}--[0-9]]/v.test('4'));
assertTrue(
    /^\p{RGI_Emoji_Tag_Sequence}$/v.test('🏴󠁧󠁢󠁳󠁣󠁴󠁿'));
assertFalse(
    /^[\p{RGI_Emoji_Tag_Sequence}--\q{🏴󠁧󠁢󠁳󠁣󠁴󠁿}]$/v.test(
        '🏴󠁧󠁢󠁳󠁣󠁴󠁿'));
assertTrue(/[\p{Script_Extensions=Greek}&&\p{Letter}]/v.test('π'));
assertFalse(/[\p{Script_Extensions=Greek}&&\p{Letter}]/v.test('𐆊'));
assertTrue(/[\p{White_Space}&&\p{ASCII}]/v.test('\n'));
assertFalse(/[\p{White_Space}&&\p{ASCII}]/v.test('\u2028'));
assertTrue(/[\p{Script_Extensions=Mongolian}&&\p{Number}]/v.test('᠗'));
assertFalse(/[\p{Script_Extensions=Mongolian}&&\p{Number}]/v.test('ᠴ'));
assertTrue(/^[\p{Emoji_Keycap_Sequence}\p{ASCII}\q{🇧🇪|abc}xyz0-9]$/v.test(
    '4️⃣'));
assertTrue(
    /^[\p{Emoji_Keycap_Sequence}\p{ASCII}\q{🇧🇪|abc}xyz0-9]$/v.test('_'));
assertTrue(
    /^[\p{Emoji_Keycap_Sequence}\p{ASCII}\q{🇧🇪|abc}xyz0-9]$/v.test('🇧🇪'));
assertTrue(/^[\p{Emoji_Keycap_Sequence}\p{ASCII}\q{🇧🇪|abc}xyz0-9]$/v.test(
    'abc'));
assertTrue(
    /^[\p{Emoji_Keycap_Sequence}\p{ASCII}\q{🇧🇪|abc}xyz0-9]$/v.test('x'));
assertTrue(
    /^[\p{Emoji_Keycap_Sequence}\p{ASCII}\q{🇧🇪|abc}xyz0-9]$/v.test('4'));
assertTrue(
    /[\p{RGI_Emoji_Flag_Sequence}\p{RGI_Emoji_Tag_Sequence}]/v.test('🇧🇪'));
assertTrue(/[\p{RGI_Emoji_Flag_Sequence}\p{RGI_Emoji_Tag_Sequence}]/v.test(
    '🏴󠁧󠁢󠁥󠁮󠁧󠁿'));
assertTrue(
    /[\p{RGI_Emoji_Flag_Sequence}\p{RGI_Emoji_Tag_Sequence}]/v.test('🇨🇭'));
assertTrue(/[\p{RGI_Emoji_Flag_Sequence}\p{RGI_Emoji_Tag_Sequence}]/v.test(
    '🏴󠁧󠁢󠁷󠁬󠁳󠁿'));

// Check new case-folding semantics.
assertEquals('XXXXXX4#', 'aAbBcC4#'.replaceAll(/\p{Lowercase_Letter}/giv, 'X'));
assertEquals('XXXXXX4#', 'aAbBcC4#'.replaceAll(/[^\P{Lowercase_Letter}]/giv, 'X'));
assertFalse(/\P{ASCII}/iv.test('K'));
assertFalse(/^\P{Lowercase}/iv.test('A'));
