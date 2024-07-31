// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

// This is a regression test where the side data got out of sync.
(async () => {
  // Will fail because of TDZ.
  assertThrowsAsync(%RuntimeEvaluateREPL('myLet; let myLet = 42;'));
  assertThrowsAsync(%RuntimeEvaluateREPL('myLet = 8;'));
  await %RuntimeEvaluateREPL('let myLet = 33;');
})().catch(e => {
  %AbortJS("Async test is failing");
});
