// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-staging

function toRangeWrapper(is_big) {
  return function _toRangeWrapped(raw_value) {
    var raw_range = this.max - this.min + (is_big ? 1n : 1);
    let range = is_big ? BigInt(raw_range) : raw_range;
    let value = is_big ? BigInt(raw_value) : raw_value;
    while (value < this.min) {
      value += range;
    }
    while (value > this.max) {
      value -= range;
    }
    return value;
  }
}

function makeConstructorObject(constr, min, max, toRange) {
  var o = {constr: constr, min: min, max: max};
  let is_big = constr.name.startsWith('Big')
  o.toRange = toRangeWrapper(is_big).bind(o);
  return o;
}

function IsBig(t) {
  return t.constructor.name.startsWith('Big');
}

function MaybeIntToBigInt(arr, i) {
  if (IsBig(arr)) {
    return BigInt(i);
  } else {
    return i;
  }
}

var IntegerTypedArrayConstructors = [
  makeConstructorObject(Int8Array, -128, 127),
  makeConstructorObject(Int16Array, -32768, 32767),
  makeConstructorObject(Int32Array, -0x80000000, 0x7fffffff),
  makeConstructorObject(BigInt64Array, -0x8000_0000_0000_0000n,
                        0x7fff_ffff_ffff_ffffn),
  makeConstructorObject(BigUint64Array, 0n, 0xffff_ffff_ffff_ffffn),
  makeConstructorObject(Uint8Array, 0, 255),
  makeConstructorObject(Uint16Array, 0, 65535),
  makeConstructorObject(Uint32Array, 0, 0xffffffff),
];

(function TestBadArray() {
  var ab = new ArrayBuffer(16);
  var sab = new SharedArrayBuffer(128);
  var sf16a = new Float16Array(sab);
  var sf32a = new Float32Array(sab);
  var sf64a = new Float64Array(sab);
  var u8ca = new Uint8ClampedArray(sab);

  // Atomic ops required integer typed arrays
  var badArrayTypes = [
    undefined, 1, 'hi', 3.4, ab, sab, sf16a, sf32a, sf64a, u8ca
  ];
  badArrayTypes.forEach(function(o) {
    assertThrows(function() { Atomics.compareExchange(o, 0, 0, 0); },
                 TypeError);
    assertThrows(function() { Atomics.load(o, 0); }, TypeError);
    assertThrows(function() { Atomics.store(o, 0, 0); }, TypeError);
    assertThrows(function() { Atomics.add(o, 0, 0); }, TypeError);
    assertThrows(function() { Atomics.sub(o, 0, 0); }, TypeError);
    assertThrows(function() { Atomics.and(o, 0, 0); }, TypeError);
    assertThrows(function() { Atomics.or(o, 0, 0); }, TypeError);
    assertThrows(function() { Atomics.xor(o, 0, 0); }, TypeError);
    assertThrows(function() { Atomics.exchange(o, 0, 0); }, TypeError);
  });
})();

(function TestBadIndex() {
  var sab = new SharedArrayBuffer(8);
  var si32a = new Int32Array(sab);
  var si32a2 = new Int32Array(sab, 4);

  // Indexes that are out of bounds when coerced via ToIndex should throw
  // RangeError.
  [-Infinity, Infinity].forEach(function(i) {
    assertThrows(function() { Atomics.compareExchange(si32a, i, 0); },
                 RangeError);
    assertThrows(function() { Atomics.load(si32a, i, 0); }, RangeError);
    assertThrows(function() { Atomics.store(si32a, i, 0); }, RangeError);
    assertThrows(function() { Atomics.add(si32a, i, 0); }, RangeError);
    assertThrows(function() { Atomics.sub(si32a, i, 0); }, RangeError);
    assertThrows(function() { Atomics.and(si32a, i, 0); }, RangeError);
    assertThrows(function() { Atomics.or(si32a, i, 0); }, RangeError);
    assertThrows(function() { Atomics.xor(si32a, i, 0); }, RangeError);
    assertThrows(function() { Atomics.exchange(si32a, i, 0); }, RangeError);
  }, RangeError);

  // Out-of-bounds indexes should throw RangeError.
  [-1, 2, 100].forEach(function(i) {
    assertThrows(function() { Atomics.compareExchange(si32a, i, 0, 0); },
                 RangeError);
    assertThrows(function() { Atomics.load(si32a, i); }, RangeError);
    assertThrows(function() { Atomics.store(si32a, i, 0); }, RangeError);
    assertThrows(function() { Atomics.add(si32a, i, 0); }, RangeError);
    assertThrows(function() { Atomics.sub(si32a, i, 0); }, RangeError);
    assertThrows(function() { Atomics.and(si32a, i, 0); }, RangeError);
    assertThrows(function() { Atomics.or(si32a, i, 0); }, RangeError);
    assertThrows(function() { Atomics.xor(si32a, i, 0); }, RangeError);
    assertThrows(function() { Atomics.exchange(si32a, i, 0); }, RangeError);
  }, RangeError);

  // Out-of-bounds indexes for array with offset should throw RangeError.
  [-1, 1, 100].forEach(function(i) {
    assertThrows(function() { Atomics.compareExchange(si32a2, i, 0, 0); });
    assertThrows(function() { Atomics.load(si32a2, i); }, RangeError);
    assertThrows(function() { Atomics.store(si32a2, i, 0); }, RangeError);
    assertThrows(function() { Atomics.add(si32a2, i, 0); }, RangeError);
    assertThrows(function() { Atomics.sub(si32a2, i, 0); }, RangeError);
    assertThrows(function() { Atomics.and(si32a2, i, 0); }, RangeError);
    assertThrows(function() { Atomics.or(si32a2, i, 0); }, RangeError);
    assertThrows(function() { Atomics.xor(si32a2, i, 0); }, RangeError);
    assertThrows(function() { Atomics.exchange(si32a2, i, 0); }, RangeError);
  });

  // Monkey-patch length and make sure these functions still throw.
  Object.defineProperty(si32a, 'length', {get: function() { return 1000; }});
  [2, 100].forEach(function(i) {
    assertThrows(function() { Atomics.compareExchange(si32a, i, 0, 0); });
    assertThrows(function() { Atomics.load(si32a, i); });
    assertThrows(function() { Atomics.store(si32a, i, 0); });
    assertThrows(function() { Atomics.add(si32a, i, 0); });
    assertThrows(function() { Atomics.sub(si32a, i, 0); });
    assertThrows(function() { Atomics.and(si32a, i, 0); });
    assertThrows(function() { Atomics.or(si32a, i, 0); });
    assertThrows(function() { Atomics.xor(si32a, i, 0); });
    assertThrows(function() { Atomics.exchange(si32a, i, 0); });
  });
})();

(function TestGoodIndex() {
  var sab = new SharedArrayBuffer(64);
  var si32a = new Int32Array(sab);
  var si32a2 = new Int32Array(sab, 32);

  var testOp = function(op, ia, index, expectedIndex, name) {
    for (var i = 0; i < ia.length; ++i)
      ia[i] = i * 2;

    ia[expectedIndex] = 0;
    var result = op(ia, index, 0, 0);
    assertEquals(0, result, name);
    assertEquals(0, ia[expectedIndex], name);

    for (var i = 0; i < ia.length; ++i) {
      if (i == expectedIndex) continue;
      assertEquals(i * 2, ia[i], name);
    }
  };

  // These values all map to index 0
  [-0, 0, 0.0, null, false, NaN, {}, '0.2', 'hi', undefined].forEach(
      function(i) {
    var name = String(i);
    [si32a, si32a2].forEach(function(array) {
      testOp(Atomics.compareExchange, array, i, 0, name);
      testOp(Atomics.load, array, i, 0, name);
      testOp(Atomics.store, array, i, 0, name);
      testOp(Atomics.add, array, i, 0, name);
      testOp(Atomics.sub, array, i, 0, name);
      testOp(Atomics.and, array, i, 0, name);
      testOp(Atomics.or, array, i, 0, name);
      testOp(Atomics.xor, array, i, 0, name);
      testOp(Atomics.exchange, array, i, 0, name);
    });
  });

  // These values all map to index 3
  var valueOf = {valueOf: function(){ return 3;}};
  var toString = {toString: function(){ return '3';}};
  [3, 3.0, '3', '3.0', valueOf, toString].forEach(function(i) {
    var name = String(i);
    [si32a, si32a2].forEach(function(array) {
      testOp(Atomics.compareExchange, array, i, 3, name);
      testOp(Atomics.load, array, i, 3, name);
      testOp(Atomics.store, array, i, 3, name);
      testOp(Atomics.add, array, i, 3, name);
      testOp(Atomics.sub, array, i, 3, name);
      testOp(Atomics.and, array, i, 3, name);
      testOp(Atomics.or, array, i, 3, name);
      testOp(Atomics.xor, array, i, 3, name);
      testOp(Atomics.exchange, array, i, 3, name);
    });
  });
})();

function clearArray(sab) {
  var ui8 = new Uint8Array(sab);
  for (var i = 0; i < sab.byteLength; ++i) {
    ui8[i] = 0;
  }
}

(function TestCompareExchange() {
  IntegerTypedArrayConstructors.forEach(function(t) {
    var sab = new SharedArrayBuffer(10 * t.constr.BYTES_PER_ELEMENT);
    var sta = new t.constr(sab);
    var sta2 = new t.constr(sab, 5 * t.constr.BYTES_PER_ELEMENT);

    [sta, sta2].forEach(function(array) {
      let _i = (i) => { return MaybeIntToBigInt(array, i); }
      clearArray(array.buffer);
      var name = Object.prototype.toString.call(array);
      for (var i = 0; i < array.length; ++i) {
        // array[i] == 0, CAS will store
        assertEquals(_i(0), Atomics.compareExchange(array, i, _i(0), _i(50)),
                    name);
        assertEquals(_i(50), array[i], name);

        // array[i] == 50, CAS will not store
        assertEquals(_i(50), Atomics.compareExchange(array, i, _i(0), _i(100)),
                     name);
        assertEquals(_i(50), array[i], name);
      }
    })
  });
})();

(function TestLoad() {
  IntegerTypedArrayConstructors.forEach(function(t) {
    var sab = new SharedArrayBuffer(10 * t.constr.BYTES_PER_ELEMENT);
    var sta = new t.constr(sab);
    var sta2 = new t.constr(sab, 5 * t.constr.BYTES_PER_ELEMENT);

    [sta, sta2].forEach(function(array) {
      let _i = (i) => { return MaybeIntToBigInt(array, i); }
      clearArray(array.buffer);
      var name = Object.prototype.toString.call(array);
      for (var i = 0; i < array.length; ++i) {
        array[i] = _i(0);
        assertEquals(_i(0), Atomics.load(array, i), name);
        array[i] = _i(50);
        assertEquals(_i(50), Atomics.load(array, i), name);
      }
    })
  });

  // Test Smi range
  (function () {
    var sab = new SharedArrayBuffer(4);
    var i32 = new Int32Array(sab);
    var u32 = new Uint32Array(sab);

    function testLoad(signedValue, unsignedValue) {
      u32[0] = unsignedValue;
      assertEquals(unsignedValue, Atomics.load(u32, 0));
      assertEquals(signedValue, Atomics.load(i32, 0));
    }

    testLoad(0x3fffffff,  0x3fffffff); // 2**30-1 (always smi)
    testLoad(0x40000000,  0x40000000); // 2**30 (smi if signed and 32-bits)
    testLoad(0x80000000, -0x80000000); // 2**31 (smi if signed and 32-bits)
    testLoad(0xffffffff, -1);          // 2**31 (smi if signed)
  });
})();

(function TestStore() {
  IntegerTypedArrayConstructors.forEach(function(t) {
    var sab = new SharedArrayBuffer(10 * t.constr.BYTES_PER_ELEMENT);
    var sta = new t.constr(sab);
    var sta2 = new t.constr(sab, 5 * t.constr.BYTES_PER_ELEMENT);

    [sta, sta2].forEach(function(array) {
      let _i = (i) => { return MaybeIntToBigInt(array, i); }
      clearArray(array.buffer);
      var name = Object.prototype.toString.call(array);
      for (var i = 0; i < array.length; ++i) {
        assertEquals(_i(50), Atomics.store(array, i, _i(50)), name);
        assertEquals(_i(50), array[i], name);

        assertEquals(_i(100), Atomics.store(array, i, _i(100)), name);
        assertEquals(_i(100), array[i], name);
      }
    })
  });
})();

(function TestAdd() {
  IntegerTypedArrayConstructors.forEach(function(t) {
    var sab = new SharedArrayBuffer(10 * t.constr.BYTES_PER_ELEMENT);
    var sta = new t.constr(sab);
    var sta2 = new t.constr(sab, 5 * t.constr.BYTES_PER_ELEMENT);

    [sta, sta2].forEach(function(array) {
      let _i = (i) => { return MaybeIntToBigInt(array, i); }
      clearArray(array.buffer);
      var name = Object.prototype.toString.call(array);
      for (var i = 0; i < array.length; ++i) {
        assertEquals(_i(0), Atomics.add(array, i, _i(50)), name);
        assertEquals(_i(50), array[i], name);

        assertEquals(_i(50), Atomics.add(array, i, _i(70)), name);
        assertEquals(_i(120), array[i], name);
      }
    })
  });
})();

(function TestSub() {
  IntegerTypedArrayConstructors.forEach(function(t) {
    var sab = new SharedArrayBuffer(10 * t.constr.BYTES_PER_ELEMENT);
    var sta = new t.constr(sab);
    var sta2 = new t.constr(sab, 5 * t.constr.BYTES_PER_ELEMENT);

    [sta, sta2].forEach(function(array) {
      let _i = (i) => { return MaybeIntToBigInt(array, i); }
      clearArray(array.buffer);
      var name = Object.prototype.toString.call(array);
      for (var i = 0; i < array.length; ++i) {
        array[i] = _i(120);
        assertEquals(_i(120), Atomics.sub(array, i, _i(50)), name);
        assertEquals(_i(70), array[i], name);

        assertEquals(_i(70), Atomics.sub(array, i, _i(70)), name);
        assertEquals(_i(0), array[i], name);
      }
    })
  });
})();

(function TestAnd() {
  IntegerTypedArrayConstructors.forEach(function(t) {
    var sab = new SharedArrayBuffer(10 * t.constr.BYTES_PER_ELEMENT);
    var sta = new t.constr(sab);
    var sta2 = new t.constr(sab, 5 * t.constr.BYTES_PER_ELEMENT);

    [sta, sta2].forEach(function(array) {
      let _i = (i) => { return MaybeIntToBigInt(array, i); }
      clearArray(array.buffer);
      var name = Object.prototype.toString.call(sta);
      for (var i = 0; i < array.length; ++i) {
        array[i] = _i(0x3f);
        assertEquals(_i(0x3f), Atomics.and(array, i, _i(0x30)), name);
        assertEquals(_i(0x30), array[i], name);

        assertEquals(_i(0x30), Atomics.and(array, i, _i(0x20)), name);
        assertEquals(_i(0x20), array[i], name);
      }
    })
  });
})();

(function TestOr() {
  IntegerTypedArrayConstructors.forEach(function(t) {
    var sab = new SharedArrayBuffer(10 * t.constr.BYTES_PER_ELEMENT);
    var sta = new t.constr(sab);
    var sta2 = new t.constr(sab, 5 * t.constr.BYTES_PER_ELEMENT);

    [sta, sta2].forEach(function(array) {
      let _i = (i) => { return MaybeIntToBigInt(array, i); }
      clearArray(array.buffer);
      var name = Object.prototype.toString.call(array);
      for (var i = 0; i < array.length; ++i) {
        array[i] = _i(0x30);
        assertEquals(_i(0x30), Atomics.or(array, i, _i(0x1c)), name);
        assertEquals(_i(0x3c), array[i], name);

        assertEquals(_i(0x3c), Atomics.or(array, i, _i(0x09)), name);
        assertEquals(_i(0x3d), array[i], name);
      }
    })
  });
})();

(function TestXor() {
  IntegerTypedArrayConstructors.forEach(function(t) {
    var sab = new SharedArrayBuffer(10 * t.constr.BYTES_PER_ELEMENT);
    var sta = new t.constr(sab);
    var sta2 = new t.constr(sab, 5 * t.constr.BYTES_PER_ELEMENT);

    [sta, sta2].forEach(function(array) {
      let _i = (i) => { return MaybeIntToBigInt(array, i); }
      clearArray(array.buffer);
      var name = Object.prototype.toString.call(array);
      for (var i = 0; i < array.length; ++i) {
        array[i] = _i(0x30);
        assertEquals(_i(0x30), Atomics.xor(array, i, _i(0x1c)), name);
        assertEquals(_i(0x2c), array[i], name);

        assertEquals(_i(0x2c), Atomics.xor(array, i, _i(0x09)), name);
        assertEquals(_i(0x25), array[i], name);
      }
    })
  });
})();

(function TestExchange() {
  IntegerTypedArrayConstructors.forEach(function(t) {
    var sab = new SharedArrayBuffer(10 * t.constr.BYTES_PER_ELEMENT);
    var sta = new t.constr(sab);
    var sta2 = new t.constr(sab, 5 * t.constr.BYTES_PER_ELEMENT);

    [sta, sta2].forEach(function(array) {
      let _i = (i) => { return MaybeIntToBigInt(array, i); }
      clearArray(array.buffer);
      var name = Object.prototype.toString.call(array);
      for (var i = 0; i < array.length; ++i) {
        array[i] = _i(0x30);
        assertEquals(_i(0x30), Atomics.exchange(array, i, _i(0x1c)), name);
        assertEquals(_i(0x1c), array[i], name);

        assertEquals(_i(0x1c), Atomics.exchange(array, i, _i(0x09)), name);
        assertEquals(_i(0x09), array[i], name);
      }
    })
  });
})();

(function TestIsLockFree() {
  // Various invalid cases.
  var valueOf = {valueOf: function(){ return 3;}};
  var toString = {toString: function(){ return '3';}};
  var invalid = [3.14, 'foo', Infinity, NaN, false, undefined, valueOf,
                 toString];
  invalid.forEach(function(v) {
    assertEquals(false, Atomics.isLockFree(v), JSON.stringify(v));
  });

  // For all platforms we support, 1, 2 and 4 bytes should be lock-free.
  assertTrue(Atomics.isLockFree(1));
  assertTrue(Atomics.isLockFree(2));
  assertTrue(Atomics.isLockFree(4));

  assertFalse(Atomics.isLockFree(0));
  assertFalse(Atomics.isLockFree(3));
  assertFalse(Atomics.isLockFree(5));
  assertFalse(Atomics.isLockFree(6));
  assertFalse(Atomics.isLockFree(7));
  // isLockFree(8) is platform dependent.
  for (var i = 9; i < 100; ++i) assertFalse(Atomics.isLockFree(i));
})();

(function TestToNumber() {
  IntegerTypedArrayConstructors.forEach(function(t) {
    var sab = new SharedArrayBuffer(1 * t.constr.BYTES_PER_ELEMENT);
    var sta = new t.constr(sab);
    let _i = (i) => { return MaybeIntToBigInt(sta, i); }

    var valueOf = {valueOf: function(){ return _i(3);}};
    var toString = {toString: function(){ return '3';}};

    [false, true, undefined, valueOf, toString].forEach(function(v) {
      if (v === undefined && IsBig(sta)) {
        // undefined does not convert to a BigInt.
        return;
      }
      let _v = () => { return IsBig(sta) ? _i(v) : (v|0); }
      var name = Object.prototype.toString.call(sta) + ' - ' + v;

      // CompareExchange
      sta[0] = _i(50);
      assertEquals(_i(50), Atomics.compareExchange(sta, 0, v, v), name);

      // Store
      assertEquals(_v(), Atomics.store(sta, 0, v), name);
      assertEquals(_v(), sta[0], name);

      // Add
      sta[0] = _i(120);
      assertEquals(_i(120), Atomics.add(sta, 0, v), name);
      assertEquals(_i(120) + _v(), sta[0], name);

      // Sub
      sta[0] = _i(70);
      assertEquals(_i(70), Atomics.sub(sta, 0, v), name);
      assertEquals(_i(70) - _v(), sta[0]);

      // And
      sta[0] = _i(0x20);
      assertEquals(_i(0x20), Atomics.and(sta, 0, v), name);
      assertEquals(_i(0x20) & _v(), sta[0]);

      // Or
      sta[0] = _i(0x3d);
      assertEquals(_i(0x3d), Atomics.or(sta, 0, v), name);
      assertEquals(_i(0x3d) | _v(), sta[0]);

      // Xor
      sta[0] = _i(0x25);
      assertEquals(_i(0x25), Atomics.xor(sta, 0, v), name);
      assertEquals(_i(0x25) ^ _v(), sta[0]);

      // Exchange
      sta[0] = _i(0x09);
      assertEquals(_i(0x09), Atomics.exchange(sta, 0, v), name);
      assertEquals(_v(), sta[0]);
    });
  });
})();

(function TestWrapping() {
  IntegerTypedArrayConstructors.forEach(function(t) {
    var sab = new SharedArrayBuffer(10 * t.constr.BYTES_PER_ELEMENT);
    var sta = new t.constr(sab);
    var name = Object.prototype.toString.call(sta);
    let _i = (i) => { return MaybeIntToBigInt(sta, i); }
    var range = t.max - t.min + _i(1);
    var offset;
    var operand;
    var val, newVal;
    var valWrapped, newValWrapped;

    for (offset = -range; offset <= range; offset += range) {
      // CompareExchange
      sta[0] = val = _i(0);
      newVal = val + offset + _i(1);
      newValWrapped = t.toRange(newVal);
      assertEquals(val, Atomics.compareExchange(sta, 0, val, newVal), name);
      assertEquals(newValWrapped, sta[0], name);

      sta[0] = val = t.min;
      newVal = val + offset - _i(1);
      newValWrapped = t.toRange(newVal);
      assertEquals(val, Atomics.compareExchange(sta, 0, val, newVal), name);
      assertEquals(newValWrapped, sta[0], name);

      // Store
      sta[0] = _i(0);
      val = t.max + offset + _i(1);
      valWrapped = t.toRange(val);
      assertEquals(val, Atomics.store(sta, 0, val), name);
      assertEquals(valWrapped, sta[0], name);

      sta[0] = val = t.min + offset - _i(1);
      valWrapped = t.toRange(val);
      assertEquals(val, Atomics.store(sta, 0, val), name);
      assertEquals(valWrapped, sta[0], name);

      // Add
      sta[0] = val = t.max;
      operand = offset + _i(1);
      valWrapped = t.toRange(val + operand);
      assertEquals(val, Atomics.add(sta, 0, operand), name);
      assertEquals(valWrapped, sta[0], name);

      sta[0] = val = t.min;
      operand = offset - _i(1);
      valWrapped = t.toRange(val + operand);
      assertEquals(val, Atomics.add(sta, 0, operand), name);
      assertEquals(valWrapped, sta[0], name);

      // Sub
      sta[0] = val = t.max;
      operand = offset - _i(1);
      valWrapped = t.toRange(val - operand);
      assertEquals(val, Atomics.sub(sta, 0, operand), name);
      assertEquals(valWrapped, sta[0], name);

      sta[0] = val = t.min;
      operand = offset + _i(1);
      valWrapped = t.toRange(val - operand);
      assertEquals(val, Atomics.sub(sta, 0, operand), name);
      assertEquals(valWrapped, sta[0], name);

      // There's no way to wrap results with logical operators, just test that
      // using an out-of-range value is properly wrapped/clamped when written
      // to memory.

      // And
      sta[0] = val = _i(0xf);
      operand = _i(0x3) + offset;
      valWrapped = t.toRange(val & operand);
      assertEquals(val, Atomics.and(sta, 0, operand), name);
      assertEquals(valWrapped, sta[0], name);

      // Or
      sta[0] = val = _i(0x12);
      operand = _i(0x22) + offset;
      valWrapped = t.toRange(val | operand);
      assertEquals(val, Atomics.or(sta, 0, operand), name);
      assertEquals(valWrapped, sta[0], name);

      // Xor
      sta[0] = val = _i(0x12);
      operand = _i(0x22) + offset;
      valWrapped = t.toRange(val ^ operand);
      assertEquals(val, Atomics.xor(sta, 0, operand), name);
      assertEquals(valWrapped, sta[0], name);

      // Exchange
      sta[0] = val = _i(0x12);
      operand = _i(0x22) + offset;
      valWrapped = t.toRange(operand);
      assertEquals(val, Atomics.exchange(sta, 0, operand), name);
      assertEquals(valWrapped, sta[0], name);
    }

  });
})();

(function TestValidateIndexBeforeValue() {
  var testOp = function(op, sta, name) {
    var valueof_has_been_called = 0;
    var value = {valueOf: function() { valueof_has_been_called = 1; return 0;}};
    var index = -1;

    // The index should be checked before calling ToInteger on the value, so
    // valueof_has_been_called should not be modified.
    sta[0] = MaybeIntToBigInt(sta, 0);
    assertThrows(function() { op(sta, index, value, value); }, RangeError);
    assertEquals(0, valueof_has_been_called);
  };

  IntegerTypedArrayConstructors.forEach(function(t) {
    var sab = new SharedArrayBuffer(10 * t.constr.BYTES_PER_ELEMENT);
    var sta = new t.constr(sab);
    var name = Object.prototype.toString.call(sta);

    testOp(Atomics.compareExchange, sta, name);
    testOp(Atomics.load, sta, name);
    testOp(Atomics.store, sta, name);
    testOp(Atomics.add, sta, name);
    testOp(Atomics.sub, sta, name);
    testOp(Atomics.and, sta, name);
    testOp(Atomics.or, sta, name);
    testOp(Atomics.xor, sta, name);
    testOp(Atomics.exchange, sta, name);
  });
})();
