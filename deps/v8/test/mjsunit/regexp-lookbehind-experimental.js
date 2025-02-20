// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --default-to-experimental-regexp-engine --experimental-regexp-engine-capture-group-opt

// Tests captures in positive and negative look-ahead in regular expressions.

function stringEscape(string) {
  // Converts string to source literal.
  return '"' + string.replace(/["\\]/g, '\\$1') + '"';
}

function testRE(re, input, expected_result) {
  assertEquals(%RegexpTypeTag(re), 'EXPERIMENTAL');

  var testName = re + '.test(' + stringEscape(input) + ')';
  if (expected_result) {
    assertTrue(re.test(input), testName);
  } else {
    assertFalse(re.test(input), testName);
  }
}

function execRE(re, input, expected_result) {
  assertEquals(%RegexpTypeTag(re), 'EXPERIMENTAL');

  var testName = re + ".exec('" + stringEscape(input) + "')";
  assertEquals(expected_result, re.exec(input), testName);
}

// Test of simple positive lookahead.

var re = /(?<=a)$/;
testRE(re, 'a', true);
testRE(re, 'b', false);
execRE(re, 'a', ['']);

re = /\wf(?<=oo\w)$/;
testRE(re, 'oof', true);
testRE(re, 'oob', false);
testRE(re, 'oaf', false);
testRE(re, 'aof', false);
execRE(re, 'oof', ['of']);

re = /(?<=\W).(?<=\w)/;
testRE(re, ' !a.', true);
testRE(re, ' !.', false);
testRE(re, ' !ba.', true);
execRE(re, ' !ba.', ['b']);

re = /..(?<=.(?<=o[^f])f)/;
testRE(re, '!oof ,', true);
testRE(re, '!of ,', false);
testRE(re, 'off ,', false);
execRE(re, '!oof ,', ['of']);

re = /^(?:a(?<=(.))|b|c)$/;
testRE(re, 'a', true);
testRE(re, 'b', true);
testRE(re, 'c', true);
testRE(re, 'd', false);
execRE(re, 'a', ['a', 'a']);
execRE(re, 'b', ['b', undefined]);
execRE(re, 'c', ['c', undefined]);

execRE(/b(?<=(b))$/, 'b', ['b', 'b']);
execRE(/^(?:(?<=(b))|a)b/, 'ab', ['ab', undefined]);
execRE(/^bd(?:(?<=(b)(?:(?<=(c))|d))|)/, 'bd', ['bd', 'b', undefined]);

// Test of Negative Look-Ahead.

re = /.(?<!x)/;
testRE(re, 'y', true);
testRE(re, 'x', false);
execRE(re, 'y', ['y']);

// Test mixed nested look-ahead with captures.

re = /(?<=(?<=(x))(y))$/;
testRE(re, 'xy', true);
testRE(re, 'xz', false);
execRE(re, 'xy', ['', 'x', 'y']);

re = /(?<!(?<!(x))(y))$/;
testRE(re, 'xy', true);
testRE(re, 'yy', false);
execRE(re, 'xy', ['', undefined, undefined]);

re = /(?<=(?<!(x))(y))$/;
testRE(re, 'yy', true);
testRE(re, 'xy', false);
execRE(re, 'yy', ['', undefined, 'y']);

re = /(?<!(?<=(x))(y))$/;
testRE(re, 'xz', true);
testRE(re, 'xy', false);
execRE(re, 'xz', ['', undefined, undefined]);

re = /(?<=(?<!(?<=(x))(y))(z))$/;
testRE(re, 'xaz', true);
testRE(re, 'ayz', true);
testRE(re, 'xyz', false);
testRE(re, 'a', false);
execRE(re, 'xaz', ['', undefined, undefined, 'z']);
execRE(re, 'ayz', ['', undefined, undefined, 'z']);

re = /(?<=(?<!(?<=(x))(y))(z))$/;
testRE(re, 'xaz', true);
testRE(re, 'ayz', true);
testRE(re, 'xyz', false);
testRE(re, 'a', false);
execRE(re, 'xaz', ['', undefined, undefined, 'z']);
execRE(re, 'ayz', ['', undefined, undefined, 'z']);

re = /(?<!(?<=(?<!(x))(y))(z))$/;
testRE(re, 'a', true);
testRE(re, 'xa', true);
testRE(re, 'xyz', true);
testRE(re, 'ayz', false);
execRE(re, 'a', ['', undefined, undefined, undefined]);
execRE(re, 'xa', ['', undefined, undefined, undefined]);
execRE(re, 'xyz', ['', undefined, undefined, undefined]);
