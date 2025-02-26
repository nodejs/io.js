// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

var ran = false;

async function test1() {
  try {
    let x = await import('modules-skip-8.mjs');
    %AbortJS('failure: should be unreachable');
  } catch(e) {
    assertEquals('x is not defined', e.message);
    ran = true;
  }
}

test1();
%PerformMicrotaskCheckpoint();
assertTrue(ran);

ran = false;

async function test2() {
  try {
    let x = await import('modules-skip-9.mjs');
    %AbortJS('failure: should be unreachable');
  } catch(e) {
    assertInstanceof(e, SyntaxError);
    assertEquals(
      "The requested module 'modules-skip-empty.mjs' does not provide an " +
      "export named 'default'",
      e.message);
    ran = true;
  }
}

test2();
%PerformMicrotaskCheckpoint();
assertTrue(ran);

ran = false;

async function test3() {
  try {
    let x = await import('nonexistent-file.mjs');
    %AbortJS('failure: should be unreachable');
  } catch(e) {
    assertTrue(e.message.startsWith('d8: Error reading'));
    ran = true;
  }
}

test3();
%PerformMicrotaskCheckpoint();
assertTrue(ran);
