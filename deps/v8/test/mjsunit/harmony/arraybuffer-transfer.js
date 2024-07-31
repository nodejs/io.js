// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function TestTransfer(method) {
  assertEquals(0, ArrayBuffer.prototype[method].length);
  assertEquals(method, ArrayBuffer.prototype[method].name);

  function AssertDetached(ab) {
    assertTrue(ab.detached);
    assertEquals(0, ab.byteLength);
    assertThrows(() => (new Uint8Array(ab)).sort(), TypeError);
  }

  function AssertResizable(originalResizable, newResizable) {
    if (method === 'transfer') {
      assertEquals(originalResizable, newResizable);
    } else {
      assertFalse(newResizable);
    }
  }

  const IndicesAndValuesForTesting = [[1, 4], [4, 18], [17, 255], [518, 48]];
  function WriteTestData(ab) {
    let u8 = new Uint8Array(ab);
    for (let [idx, val] of IndicesAndValuesForTesting) {
      u8[idx] = val;
    }
  }
  function AssertBufferContainsTestData(ab) {
    let u8 = new Uint8Array(ab);
    for (let [idx, val] of IndicesAndValuesForTesting) {
      if (idx < u8.length) {
        assertEquals(val, u8[idx]);
      }
    }
  }

  function TestSameLength(len, opts) {
    let ab = new ArrayBuffer(len, opts);
    WriteTestData(ab);
    const resizable = ab.resizable;
    const xfer = ab[method]();
    assertEquals(len, xfer.byteLength);
    AssertResizable(resizable, xfer.resizable);
    AssertBufferContainsTestData(xfer);
    AssertDetached(ab);
  }
  TestSameLength(1024);
  TestSameLength(1024, {maxByteLength: 2048});

  function TestGrow(len, opts) {
    let ab = new ArrayBuffer(len, opts);
    const resizable = ab.resizable;
    WriteTestData(ab);
    const newLen = len * 2 + 128;  // +128 to ensure newLen is never 0
    const xfer = ab[method](newLen);
    assertEquals(newLen, xfer.byteLength);
    AssertResizable(resizable, xfer.resizable);
    if (len > 0) AssertBufferContainsTestData(xfer);
    AssertDetached(ab);

    // The new memory should be zeroed.
    let u8 = new Uint8Array(xfer);
    for (let i = len; i < newLen; i++) {
      assertEquals(0, u8[i]);
    }
  }
  TestGrow(1024);
  TestGrow(0);
  if (method === 'transfer') {
    // Cannot transfer to a new byte length > max byte length.
    assertThrows(() => TestGrow(1024, {maxByteLength: 2048}), RangeError);
  } else {
    TestGrow(1024, {maxByteLength: 2048});
  }
  TestGrow(0, {maxByteLength: 2048});

  function TestNonGrow(len, opts) {
    for (let newLen
             of [len / 2,  // shrink
                 0         // 0 special case
    ]) {
      let ab = new ArrayBuffer(len, opts);
      WriteTestData(ab);
      const resizable = ab.resizable;
      const xfer = ab[method](newLen);
      assertEquals(newLen, xfer.byteLength);
      AssertResizable(resizable, xfer.resizable);
      if (len > 0) AssertBufferContainsTestData(xfer);
      AssertDetached(ab);
    }
  }
  TestNonGrow(1024);
  TestNonGrow(1024, {maxByteLength: 2048});
  TestNonGrow(0);
  TestNonGrow(0, {maxByteLength: 2048});

  (function TestParameterConversion() {
    const len = 1024;
    {
      let ab = new ArrayBuffer(len);
      const detach = {
        valueOf() {
          %ArrayBufferDetach(ab);
          return len;
        }
      };
      assertThrows(() => ab[method](detach), TypeError);
    }

    {
      let ab = new ArrayBuffer(len, {maxByteLength: len * 4});
      const resizable = ab.resizable;
      const shrink = {
        valueOf() {
          ab.resize(len / 2);
          return len;
        }
      };
      const xfer = ab[method](shrink);
      AssertResizable(resizable, xfer.resizable);
      assertEquals(len, xfer.byteLength);
    }

    {
      let ab = new ArrayBuffer(len, {maxByteLength: len * 4});
      const resizable = ab.resizable;
      const grow = {
        valueOf() {
          ab.resize(len * 2);
          return len;
        }
      };
      const xfer = ab[method](grow);
      AssertResizable(resizable, xfer.resizable);
      assertEquals(len, xfer.byteLength);
    }
  })();

  (function TestCannotBeSAB() {
    const len = 1024;
    let sab = new SharedArrayBuffer(1024);
    let gsab = new SharedArrayBuffer(len, {maxByteLength: len * 4});

    assertThrows(() => ArrayBuffer.prototype[method].call(sab), TypeError);
    assertThrows(() => ArrayBuffer.prototype[method].call(gsab), TypeError);
  })();

  (function TestInvalidLength() {
    for (let newLen of [-1024, Number.MAX_SAFE_INTEGER + 1]) {
      let ab = new ArrayBuffer(1024);
      assertThrows(() => ab[method](newLen), RangeError);
    }
  })();

  (function TestEmptySourceStore() {
    let ab = new ArrayBuffer();
    let xfer = ab[method]()[method](1024);
  })();

  if (typeof WebAssembly !== 'undefined') {
    // WebAssembly buffers cannot be detached.
    const memory = new WebAssembly.Memory({initial: 1});
    assertThrows(() => memory.buffer[method](), TypeError);
  }
}

TestTransfer('transfer');
TestTransfer('transferToFixedLength');
