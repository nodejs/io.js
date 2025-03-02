// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

load("test/mjsunit/regress-393408781-2.js");

let slot = 405;     // Create a Smi slot.
slot = -1795162112; // Transition to a mutable heap number.

function slot_load() {
  return slot;
}

// Create a load from a script context.
let value = bar("(" + slot_load.toString() + ")")();
assertEquals(-1795162112, value);
