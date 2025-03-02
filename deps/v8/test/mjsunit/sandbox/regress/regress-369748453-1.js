// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --expose-gc --expose-externalize-string --sandbox-testing

const kSeqStringType = Sandbox.getInstanceTypeIdFor("SEQ_ONE_BYTE_STRING_TYPE");
const kStringLengthOffset = Sandbox.getFieldOffset(kSeqStringType, "length");

let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

let string = "foo" + "bar" + "baz";
assertEquals(Sandbox.getInstanceTypeIdOf(string), kSeqStringType);

let string_address = Sandbox.getAddressOf(string);
let orig_length = memory.getUint32(string_address + kStringLengthOffset, true);
assertEquals(orig_length, string.length);
let corrupted_length = Math.floor(Math.random() * 0x100000000);
memory.setUint32(string_address + kStringLengthOffset, corrupted_length, true);

// Externalization is one way to trigger a WriteToFlat on an external buffer.
externalizeString(string);
