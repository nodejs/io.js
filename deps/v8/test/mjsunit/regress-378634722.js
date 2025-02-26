// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let val = 42; // Slot is constant.
function foo(v) { val = v; }
foo(4.2); // Slot is mutable heap number.
foo(0.0); // Set slot to 0.
foo(-0.0); // Set slot to -0.
assertEquals(-0.0, val);
