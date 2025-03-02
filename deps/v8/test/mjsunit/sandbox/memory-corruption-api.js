// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --expose-memory-corruption-api

const kHeapObjectTag = 0x1;

assertSame(typeof Sandbox.base, 'number');
assertSame(typeof Sandbox.byteLength, 'number');

let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

let obj = {a: 42};

//
// Sandbox.isWritable
//
assertTrue(Sandbox.isWritable(obj));
// Here we assume that certain builtin objects (e.g. the 'undefined' value) are
// located in read-only space.
assertFalse(Sandbox.isWritable(undefined));

//
// Sandbox.getAddressOf and Sandbox.getObjectAt
//
let addr = Sandbox.getAddressOf(obj);
assertSame(typeof addr, 'number');

let sameObj = Sandbox.getObjectAt(addr);
assertSame(obj, sameObj);

//
// Sandbox reading + writing
//
// Here we assume that .a will be an inline property at offset 12. We could
// also use other types of objects (e.g. inline TypedArrays) or so if that
// every changes.
const kOffsetOfPropertyA = 12;
let currentValue = memory.getUint32(addr + kOffsetOfPropertyA, true);
assertEquals(currentValue, 42 << 1);

let newValue = 43 << 1;
memory.setUint32(addr + kOffsetOfPropertyA, newValue, true);
assertEquals(obj.a, 43);

//
// Sandbox.getSizeOf, Sandbox.getInstanceTypeOf, and Sandbox.getInstanceTypeIdOf
//
let size = Sandbox.getSizeOf(obj);
// We don't want to rely on the specific size here, but it should be reasonable.
assertTrue(size > 4 && size < 64);

let instanceType = Sandbox.getInstanceTypeOf(obj);
assertSame(typeof instanceType, 'string');
assertEquals(instanceType, 'JS_OBJECT_TYPE');

let instanceTypeId = Sandbox.getInstanceTypeIdOf(obj);
assertSame(typeof instanceTypeId, 'number');
assertEquals(instanceTypeId, Sandbox.getInstanceTypeIdFor('JS_OBJECT_TYPE'));

//
// Sandbox.isValidObjectAt
//
addr = Sandbox.getAddressOf(obj);
assertTrue(Sandbox.isValidObjectAt(addr));
assertFalse(Sandbox.isValidObjectAt(0x41414141));

// Check that internal objects are recognized by isValidObjectAt.
// Here we assume that the first three fields are pointer fields.
assertTrue(Sandbox.getSizeOf(obj) >= 12);
for (let i = 0; i < 3; i++) {
  let internalObjectAddr = memory.getUint32(addr + i*4, true);
  assertEquals(internalObjectAddr & kHeapObjectTag, kHeapObjectTag)
  assertTrue(Sandbox.isValidObjectAt(internalObjectAddr));
}

// Check that various objects are recognized by isValidObjectAt.
for (let prop of Object.getOwnPropertyNames(this)) {
  let addr = Sandbox.getAddressOf(this[prop]);
  assertTrue(Sandbox.isValidObjectAt(addr));
}

// Check that random addresses don't cause crashes in isValidObjectAt.
for (let addr = 0; addr <= 0x1000; addr++) {
  Sandbox.isValidObjectAt(addr);
}
