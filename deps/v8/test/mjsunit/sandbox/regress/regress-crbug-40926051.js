// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --sandbox-testing


let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

let array = [0.0, 1.1, 2.2, 3.3, 4.4];

// Corrupt the length of the JSArray and change it to a large value.
const kJSArrayLengthOffset = Sandbox.getFieldOffsetOf(array, "length");
memory.setUint32(
    Sandbox.getAddressOf(array) + kJSArrayLengthOffset,
    0x10000,
    true);

// Try to push nothing, which should succeed and not crash in any way.
array.push();
