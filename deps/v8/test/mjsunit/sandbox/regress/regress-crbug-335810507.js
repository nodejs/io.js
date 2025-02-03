// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --sandbox-testing

const kSlicedStringType = Sandbox.getInstanceTypeIdFor("SLICED_ONE_BYTE_STRING_TYPE");
const kSlicedStringParentOffset = Sandbox.getFieldOffset(kSlicedStringType, "parent");

let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

// Create a sliced string.
const sliced_string = "It's fun to play in the sand".substring(3);
// Create a two-byte string
const two_byte_string = "⛱️📦"

// Corrupt the parent pointer of the sliced string to point to the two-byte
// string.
memory.setUint32(
    Sandbox.getAddressOf(sliced_string) + kSlicedStringParentOffset,
    Sandbox.getAddressOf(two_byte_string),
    true);

// Observe the shenanigans!
sliced_string.toLowerCase();
