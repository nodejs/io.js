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

var re = /^(?=a)/;
testRE(re, 'a', true);
testRE(re, 'b', false);
execRE(re, 'a', ['']);

re = /^(?=\woo)f\w/;
testRE(re, 'foo', true);
testRE(re, 'boo', false);
testRE(re, 'fao', false);
testRE(re, 'foa', false);
execRE(re, 'foo', ['fo']);

re = /(?=\w).(?=\W)/;
testRE(re, '.a! ', true);
testRE(re, '.! ', false);
testRE(re, '.ab! ', true);
execRE(re, '.ab! ', ['b']);

re = /(?=f(?=[^f]o))../;
testRE(re, ', foo!', true);
testRE(re, ', fo!', false);
testRE(re, ', ffo', false);
execRE(re, ', foo!', ['fo']);

re = /^(?:(?=(.))a|b|c)$/;
testRE(re, 'a', true);
testRE(re, 'b', true);
testRE(re, 'c', true);
testRE(re, 'd', false);
execRE(re, 'a', ['a', 'a']);
execRE(re, 'b', ['b', undefined]);
execRE(re, 'c', ['c', undefined]);

execRE(/^(?=(b))b/, 'b', ['b', 'b']);
execRE(/^(?:(?=(b))|a)b/, 'ab', ['ab', undefined]);
execRE(/^(?:(?=(b)(?:(?=(c))|d))|)bd/, 'bd', ['bd', 'b', undefined]);

// Test of Negative Look-Ahead.

re = /(?!x)./;
testRE(re, 'y', true);
testRE(re, 'x', false);
execRE(re, 'y', ['y']);

re = /(?!(\d))|\d/;
testRE(re, '4', true);
execRE(re, '4', ['4', undefined]);
execRE(re, 'x', ['', undefined]);

// Test mixed nested look-ahead with captures.

re = /^(?=(x)(?=(y)))/;
testRE(re, 'xy', true);
testRE(re, 'xz', false);
execRE(re, 'xy', ['', 'x', 'y']);

re = /^(?!(x)(?!(y)))/;
testRE(re, 'xy', true);
testRE(re, 'xz', false);
execRE(re, 'xy', ['', undefined, undefined]);

re = /^(?=(x)(?!(y)))/;
testRE(re, 'xz', true);
testRE(re, 'xy', false);
execRE(re, 'xz', ['', 'x', undefined]);

re = /^(?!(x)(?=(y)))/;
testRE(re, 'xz', true);
testRE(re, 'xy', false);
execRE(re, 'xz', ['', undefined, undefined]);

re = /^(?=(x)(?!(y)(?=(z))))/;
testRE(re, 'xaz', true);
testRE(re, 'xya', true);
testRE(re, 'xyz', false);
testRE(re, 'a', false);
execRE(re, 'xaz', ['', 'x', undefined, undefined]);
execRE(re, 'xya', ['', 'x', undefined, undefined]);

re = /^(?!(x)(?=(y)(?!(z))))/;
testRE(re, 'a', true);
testRE(re, 'xa', true);
testRE(re, 'xyz', true);
testRE(re, 'xya', false);
execRE(re, 'a', ['', undefined, undefined, undefined]);
execRE(re, 'xa', ['', undefined, undefined, undefined]);
execRE(re, 'xyz', ['', undefined, undefined, undefined]);
