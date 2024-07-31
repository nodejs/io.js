// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug
// Flags: --no-baseline-batch-compilation --use-osr
// Flags: --osr-from-maglev --concurrent-osr --concurrent-recompilation
// Flags: --always-osr-from-maglev

let a = 0;
var keep_going = 100000;  // A counter to avoid test hangs on failure.

function f() {
  let reached_tf = false;
  let status = 0;
  while (!reached_tf && --keep_going) {
    a = a + 1;
    // This loop should trigger OSR.
    reached_tf = %CurrentFrameIsTurbofan();
    status = %GetOptimizationStatus(f);
  }
}

f();
