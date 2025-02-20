// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

assertEquals('a-b-c-d', 'a+b+c+d'.replaceAll('+', '-'));
assertEquals('aaaa', 'abcd'.replaceAll(/./g, 'a'));
assertEquals('', ''.replaceAll('a', 'b'));
assertEquals('b', ''.replaceAll('', 'b'));
assertEquals('_x_x_x_', 'xxx'.replaceAll('', '_'));
assertEquals('yx', 'xxx'.replaceAll('xx', 'y'));
assertEquals('xxxx', 'xx'.replaceAll('xx', '$&$&'));
assertEquals('ii', '.+*$.+*$'.replaceAll('.+*$', 'i'));

{
  // Non regexp search value with replace method.
  const nonRegExpSearchValue = {
    [Symbol.replace]: (string, replacer) => {
      assertEquals(string, 'barbar');
      assertEquals(replacer, 'moo');
      return 'foo'
    },
    toString: () => {
      // Verify toString is not called.
      unreachable();
    }
  };
  assertEquals('foo', 'barbar'.replaceAll(nonRegExpSearchValue, 'moo'));
}

{
  // A custom regexp with non coercible flags.
  class RegExpNonCoercibleFlags extends RegExp {
    constructor() {
      super();
    }

    static get [Symbol.species]() {
      return RegExp;
    }

    get flags() { return null; }
  };
  assertThrows(
      () => { assertEquals(
          'foo',
          'barbar'.replaceAll(new RegExpNonCoercibleFlags, 'moo')); },
      TypeError);
}

{
  // Non regexp search value with replace property
  const nonRegExpSearchValue = {
    [Symbol.replace]: "doh",
    toString: () => {
      // Verify toString is not called.
      unreachable();
    }
  };
  assertThrows(
      () => { 'barbar'.replaceAll(nonRegExpSearchValue, 'moo'); },
      TypeError);
}

{
  // Non callable, non string replace value.
  const nonCallableNonStringReplace = {
    toString: () => {
      return 'boo';
    },
  };
  assertEquals('booboo', 'moomoo'.replaceAll('moo', nonCallableNonStringReplace));
}

{
  const positions = [];
  assertEquals('bcb', 'aca'.replaceAll('a',
                                       (searchString, position, string) => {
    assertEquals('a', searchString);
    assertEquals('aca', string);
    positions.push(position);
    return 'b';
  }));
  assertEquals(positions, [0,2]);
}

(function NonGlobalRegex() {
  assertThrows(
      () => { 'ab'.replaceAll(/./, '.'); },
      TypeError);
  assertThrows(
      () => { 'ab'.replaceAll(/./y, '.'); },
      TypeError);
})();

// Tests for stickiness gotcha.
assertEquals('o ppercase!', 'No Uppercase!'.replaceAll(/[A-Z]/g,  ''));
assertEquals('o Uppercase?', 'No Uppercase?'.replaceAll(/[A-Z]/gy, ''));
assertEquals(' UPPERCASE!', 'NO UPPERCASE!'.replaceAll(/[A-Z]/gy, ''));

// Tests for slow path.
assertEquals('a', 'a'.replaceAll(%ConstructConsString('abcdefghijklmn',
                                                      'def'), 'b'));
assertEquals('b', 'abcdefghijklmndef'.replaceAll(
    %ConstructConsString('abcdefghijklmn', 'def'), 'b'));

assertEquals('aaaaaaaaaaaaaaaaaa', 'aaaaaaaaaaaaaaaaaa'.replaceAll(
    %ConstructConsString('abcdefghijklmn', 'def'), 'b'));
