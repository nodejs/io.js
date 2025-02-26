// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

for (let i = 0; i < 25; i++) {
  try {
    for (let j = 0; j < 10;
         (() => j++)()) { }
    undefined();
  } catch(e) { }
}
