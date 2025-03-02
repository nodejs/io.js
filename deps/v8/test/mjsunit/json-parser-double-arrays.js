// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function test(string, expected) {
  const got = JSON.parse(string);
  assertEquals(expected, got);
  assertEquals(%HasSmiElements(expected), %HasSmiElements(got));
}

function testError(string) {
  assertThrows(() => JSON.parse(string));
}

test('[]', []);
test('[  ]', []);

// SMIs
test('[1, 2, 3]', [1, 2, 3]);

// SMIs and doubles
test('[1, 2, 3.5]', [1, 2, 3.5]);
test('[1, 2.5, 3]', [1, 2.5, 3]);
test('[1.5, 2, 3]', [1.5, 2, 3]);

// SMIs, doubles and objects
test('[1, 2, 3.5, {"a": 30}]', [1, 2, 3.5, {a: 30}]);

// Whitespace
test(' [  1  ,  2 ,  3.5  ,  {  "a"  : 30 }  ]', [1, 2, 3.5, {a: 30}]);

// Arrays in objects
test('{"a": [1, 2, 3]}', {a: [1, 2, 3]});
test('{"a": [1, 2, 3.5]}', {a: [1, 2, 3.5]});
test('{"a": [1, 2, 3.5, {"a": 30}]}', {a: [1, 2, 3.5, {a: 30}]});

// This should also result in PACKED_SMI_ELEMENTS.
test('[1.0, 2.0, 3.0]', [1, 2, 3]);

// Syntax errors
testError('[1, 2');
testError('[1, 2, ]');
testError('{"a": [1, 2}');
