// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --expose-gc --sandbox-testing

const kJSArrayType = Sandbox.getInstanceTypeIdFor("JS_ARRAY_TYPE");
const kJSArrayLengthOffset = Sandbox.getFieldOffset(kJSArrayType, "length");
const kJSArrayElementsOffset = Sandbox.getFieldOffset(kJSArrayType, "elements");

const memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

// Allocate an array and promote it to the old generation.
const array = Array();
gc();
// Allocate a value in the young generation.
const value = new Number();

// Corrupt the JSArray such that
// 1. It's length becomes very large, allowing OOB writes
// 2. It's backing FixedArray points to 0x0, in RO space
let array_addr = Sandbox.getAddressOf(array);
memory.setUint32(array_addr + kJSArrayLengthOffset, 0x7ffffffe, true);
memory.setUint32(array_addr + kJSArrayElementsOffset, 0x1, true);

// OOB write to the JS array such that the write happens on a writable page
// even though the host object (supposedly a FixedArray) lives in RO space.
assertFalse(Sandbox.isWritableObjectAt(0));
assertTrue(Sandbox.isWritableObjectAt(1024 * 1024));
let index = (1024 * 1024) / 4;
array[index] = value;
