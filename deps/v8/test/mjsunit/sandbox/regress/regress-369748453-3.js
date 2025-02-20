// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --expose-gc --expose-externalize-string --sandbox-testing

const kInternalizedStringType =
  Sandbox.getInstanceTypeIdFor("INTERNALIZED_ONE_BYTE_STRING_TYPE");
const kConsStringType =
  Sandbox.getInstanceTypeIdFor("CONS_ONE_BYTE_STRING_TYPE");
const kStringLengthOffset =
  Sandbox.getFieldOffset(kInternalizedStringType, "length");
const kConsStringFirstOffset =
  Sandbox.getFieldOffset(kConsStringType, "first");
const kConsStringSecondOffset =
  Sandbox.getFieldOffset(kConsStringType, "second");

let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

let first = "first long string to make cons string";
assertEquals(Sandbox.getInstanceTypeIdOf(first), kInternalizedStringType);
let second = "second long string to make cons string";
assertEquals(Sandbox.getInstanceTypeIdOf(second), kInternalizedStringType);
const string = first + second;
assertEquals(Sandbox.getInstanceTypeIdOf(string), kConsStringType);

// String must be in old space for externalization.
assertThrows(() => externalizeString(string));
gc();
gc();

let string_address = Sandbox.getAddressOf(string);
// Corrupt the first child's length.
let first_address =
  memory.getUint32(string_address + kConsStringFirstOffset, true) - 1;
assertEquals(first_address, Sandbox.getAddressOf(first));
let orig_length = memory.getUint32(first_address + kStringLengthOffset, true);
let corrupted_length = Math.floor(Math.random() * 0x100000000);
assertEquals(orig_length, first.length);
memory.setUint32(first_address + kStringLengthOffset, corrupted_length, true);

// Corrupt the second child's length.
let second_address =
  memory.getUint32(string_address + kConsStringSecondOffset, true) - 1;
assertEquals(second_address, Sandbox.getAddressOf(second));
orig_length = memory.getUint32(second_address + kStringLengthOffset, true);
corrupted_length = Math.floor(Math.random() * 0x100000000);
assertEquals(orig_length, second.length);
memory.setUint32(second_address + kStringLengthOffset, corrupted_length, true);

// Externalization is one way to trigger a WriteToFlat on an external buffer.
externalizeString(string);
