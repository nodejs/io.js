// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --sandbox-testing

assertSame(typeof Sandbox.base, 'number');
assertSame(typeof Sandbox.byteLength, 'number');

let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

let obj = {a: 42};

assertTrue(Sandbox.isWritable(obj));
// Here we assume that certain builtin objects (e.g. the 'undefined' value) are
// located in read-only space.
assertFalse(Sandbox.isWritable(undefined));

let addr = Sandbox.getAddressOf(obj);
assertSame(typeof addr, 'number');

let sameObj = Sandbox.getObjectAt(addr);
assertSame(obj, sameObj);

// Here we assume that .a will be an inline property at offset 12. We could
// also use other types of objects (e.g. inline TypedArrays) or so if that
// every changes.
const kOffsetOfPropertyA = 12;
let currentValue = memory.getUint32(addr + kOffsetOfPropertyA, true);
assertEquals(currentValue, 42 << 1);

let newValue = 43 << 1;
memory.setUint32(addr + kOffsetOfPropertyA, newValue, true);
assertEquals(obj.a, 43);

let size = Sandbox.getSizeOf(obj);
// We don't want to rely on the specific size here, but it should be reasonable.
assertTrue(size > 4 && size < 64);

let instanceType = Sandbox.getInstanceTypeOf(obj);
assertSame(typeof instanceType, 'string');
assertEquals(instanceType, 'JS_OBJECT_TYPE');

let instanceTypeId = Sandbox.getInstanceTypeIdOf(obj);
assertSame(typeof instanceTypeId, 'number');
// We don't want to rely on the specific instance type here, but it should be a
// 16-bit value.
assertTrue(instanceTypeId >= 0 && instanceTypeId <= 0xffff);
