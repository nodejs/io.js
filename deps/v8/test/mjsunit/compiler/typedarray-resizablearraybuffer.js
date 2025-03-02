// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan
// Flags: --js-staging

"use strict";

d8.file.execute('test/mjsunit/typedarray-helpers.js');

const is_little_endian = (() => {
  var buffer = new ArrayBuffer(4);
  const HEAP32 = new Int32Array(buffer);
  const HEAPU8 = new Uint8Array(buffer);
  HEAP32[0] = 255;
  return (HEAPU8[0] === 255 && HEAPU8[3] === 0);
})();

function FillBuffer(buffer) {
  const view = new Uint8Array(buffer);
  for (let i = 0; i < view.length; ++i) {
    view[i] = i;
  }
}
%NeverOptimizeFunction(FillBuffer);

function asU16(index) {
  const start = index * 2;
  if (is_little_endian) {
    return (start + 1) * 256 + start;
  } else {
    return start * 256 + start + 1;
  }
}
%NeverOptimizeFunction(asU16);

function asU32(index) {
  const start = index * 4;
  if (is_little_endian) {
    return (((start + 3) * 256 + start + 2) * 256 + start + 1) * 256 + start;
  } else {
    return ((((start * 256) + start + 1) * 256) + start + 2) * 256 + start + 3;
  }
}
%NeverOptimizeFunction(asU32);

function asF16(index) {
  const start = index * 2;
  const ab = new ArrayBuffer(2);
  const ta = new Uint8Array(ab);
  for (let i = 0; i < 2; ++i) ta[i] = start + i;
  return new Float16Array(ab)[0];
}
%NeverOptimizeFunction(asF16);

function asF32(index) {
  const start = index * 4;
  const ab = new ArrayBuffer(4);
  const ta = new Uint8Array(ab);
  for (let i = 0; i < 4; ++i) ta[i] = start + i;
  return new Float32Array(ab)[0];
}
%NeverOptimizeFunction(asF32);

function asF64(index) {
  const start = index * 8;
  const ab = new ArrayBuffer(8);
  const ta = new Uint8Array(ab);
  for (let i = 0; i < 8; ++i) ta[i] = start + i;
  return new Float64Array(ab)[0];
}
%NeverOptimizeFunction(asF64);

function asB64(index) {
  const start = index * 8;
  let result = 0n;
  if (is_little_endian) {
    for (let i = 0; i < 8; ++i) {
      result = result << 8n;
      result += BigInt(start + 7 - i);
    }
  } else {
    for (let i = 0; i < 8; ++i) {
      result = result << 8n;
      result += BigInt(start + i);
    }
  }
  return result;
}
%NeverOptimizeFunction(asB64);

function CreateBuffer(shared, len, max_len) {
  return shared ? new SharedArrayBuffer(len, {maxByteLength: max_len}) :
                  new ArrayBuffer(len, {maxByteLength: max_len});
}
%NeverOptimizeFunction(CreateBuffer);

function MakeResize(target, shared, offset, fixed_len) {
  const bpe = target.name === 'DataView' ? 1 : target.BYTES_PER_ELEMENT;
  function RoundDownToElementSize(blen) {
    return Math.floor(blen / bpe) * bpe;
  }
  if (!shared) {
    if (fixed_len === undefined) {
      return (b, len) => {
        b.resize(len);
        const blen = Math.max(0, len - offset);
        return RoundDownToElementSize(blen);
      };
    } else {
      const fixed_blen = fixed_len * bpe;
      return (b, len) => {
        b.resize(len);
        const blen = fixed_blen <= (len - offset) ? fixed_blen : 0;
        return RoundDownToElementSize(blen);
      }
    }
  } else {
    if (fixed_len === undefined) {
      return (b, len) => {
        let blen = 0;
        if (len > b.byteLength) {
          b.grow(len);
          blen = Math.max(0, len - offset);
        } else {
          blen = b.byteLength - offset;
        }
        return RoundDownToElementSize(blen);
      };
    } else {
      return (b, len) => {
        if (len > b.byteLength) {
          b.grow(len);
        }
        return fixed_len * bpe;
      };
    }
  }
}
%NeverOptimizeFunction(MakeResize);

function MakeElement(target, offset) {
  const o = offset / target.BYTES_PER_ELEMENT;
  if (target.name === 'Int8Array') {
    return (index) => {
      return o + index;
    };
  } else if (target.name === 'Uint32Array') {
    return (index) => {
      return asU32(o + index);
    };
  } else if (target.name === 'Float64Array') {
    return (index) => {
      return asF64(o + index);
    };
  } else if (target.name === 'BigInt64Array') {
    return (index) => {
      return asB64(o + index);
    };
  } else {
    console.log(`unimplemented: MakeElement(${target.name})`);
    return () => undefined;
  }
}
%NeverOptimizeFunction(MakeElement);

function MakeCheckBuffer(target, offset) {
  return (ab, up_to) => {
    const view = new Uint8Array(ab);
    for (let i = 0; i < offset; ++i) {
      assertEquals(0, view[i]);
    }
    for (let i = 0; i < (up_to * target.BYTES_PER_ELEMENT) + 1; ++i) {
      // Use PrintBuffer(ab) for debugging.
      assertEquals(offset + i, view[offset + i]);
    }
  }
}
%NeverOptimizeFunction(MakeCheckBuffer);

function ClearBuffer(ab) {
  for (let i = 0; i < ab.byteLength; ++i) ab[i] = 0;
}
%NeverOptimizeFunction(ClearBuffer);

// Use this for debugging these tests.
function PrintBuffer(buffer) {
  const view = new Uint8Array(buffer);
  for (let i = 0; i < 32; ++i) {
    console.log(`[${i}]: ${view[i]}`)
  }
}
%NeverOptimizeFunction(PrintBuffer);

// Detach a buffer to trip the protector up front, to prevent deopts in later
// tests.
%ArrayBufferDetach(new ArrayBuffer(8));

(function() {
for (let shared of [false, true]) {
  for (let length_tracking of [false, true]) {
    for (let with_offset of [false, true]) {
      for (let target
               of [Int8Array, Uint32Array, Float64Array, BigInt64Array]) {
        const test_case = `Testing: Length_${shared ? 'GSAB' : 'RAB'}_${
            length_tracking ? 'LengthTracking' : 'FixedLength'}${
            with_offset ? 'WithOffset' : ''}_${target.name}`;
        // console.log(test_case);

        const byte_length_code = 'return ta.byteLength; // ' + test_case;
        const ByteLength = new Function('ta', byte_length_code);
        const length_code = 'return ta.length; // ' + test_case;
        const Length = new Function('ta', length_code);
        const offset = with_offset ? 8 : 0;

        let blen = 16 - offset;
        const fixed_len =
            length_tracking ? undefined : (blen / target.BYTES_PER_ELEMENT);
        const ab = CreateBuffer(shared, 16, 40);
        const ta = new target(ab, offset, fixed_len);
        const Resize = MakeResize(target, shared, offset, fixed_len);

        assertUnoptimized(ByteLength);
        assertUnoptimized(Length);
        %PrepareFunctionForOptimization(ByteLength);
        %PrepareFunctionForOptimization(Length);
        assertEquals(blen, ByteLength(ta));
        assertEquals(blen, ByteLength(ta));
        assertEquals(Math.floor(blen / target.BYTES_PER_ELEMENT), Length(ta));
        assertEquals(Math.floor(blen / target.BYTES_PER_ELEMENT), Length(ta));
        %OptimizeFunctionOnNextCall(ByteLength);
        %OptimizeFunctionOnNextCall(Length);
        assertEquals(blen, ByteLength(ta));
        assertEquals(Math.floor(blen / target.BYTES_PER_ELEMENT), Length(ta));
        blen = Resize(ab, 32);
        assertEquals(blen, ByteLength(ta));
        assertEquals(Math.floor(blen / target.BYTES_PER_ELEMENT), Length(ta));
        blen = Resize(ab, 9);
        assertEquals(blen, ByteLength(ta));
        assertEquals(Math.floor(blen / target.BYTES_PER_ELEMENT), Length(ta));
        blen = Resize(ab, 24);
        assertEquals(blen, ByteLength(ta));
        assertEquals(Math.floor(blen / target.BYTES_PER_ELEMENT), Length(ta));

        if (!shared) {
          %ArrayBufferDetach(ab);
          assertEquals(0, ByteLength(ta));
          assertEquals(0, Length(ta));
          assertOptimized(Length);
        }
      }
    }
  }
}
})();

(function() {
for (let shared of [false, true]) {
  for (let length_tracking of [false, true]) {
    for (let with_offset of [false, true]) {
      for (let target
               of [Int8Array, Uint32Array, Float64Array, BigInt64Array]) {
        const test_case = `Testing: Read_${shared ? 'GSAB' : 'RAB'}_${
            length_tracking ? 'LengthTracking' : 'FixedLength'}${
            with_offset ? 'WithOffset' : ''}_${target.name}`;
        // console.log(test_case);

        const read_code = 'return ta[index]; // ' + test_case;
        const Read = new Function('ta', 'index', read_code);
        const offset = with_offset ? 8 : 0;

        let blen = 16 - offset;
        let len = Math.floor(blen / target.BYTES_PER_ELEMENT);
        const fixed_len = length_tracking ? undefined : len;
        const ab = CreateBuffer(shared, 16, 40);
        const ta = new target(ab, offset, fixed_len);
        const Resize = MakeResize(target, shared, offset, fixed_len);
        const Element = MakeElement(target, offset);
        FillBuffer(ab);

        assertUnoptimized(Read);
        %PrepareFunctionForOptimization(Read);
        for (let i = 0; i < len * 2; ++i)
          assertEquals(i < len ? Element(i) : undefined, Read(ta, i));
        %OptimizeFunctionOnNextCall(Read);
        for (let i = 0; i < len * 2; ++i)
          assertEquals(i < len ? Element(i) : undefined, Read(ta, i));
        assertOptimized(Read);
        blen = Resize(ab, 32);
        FillBuffer(ab);
        len = Math.floor(blen / target.BYTES_PER_ELEMENT);
        for (let i = 0; i < len * 2; ++i)
          assertEquals(i < len ? Element(i) : undefined, Read(ta, i));
        assertOptimized(Read);
        blen = Resize(ab, 9);
        FillBuffer(ab);
        len = Math.floor(blen / target.BYTES_PER_ELEMENT);
        for (let i = 0; i < len * 2; ++i)
          assertEquals(i < len ? Element(i) : undefined, Read(ta, i));
        assertOptimized(Read);
        blen = Resize(ab, 0);
        len = Math.floor(blen / target.BYTES_PER_ELEMENT);
        for (let i = 0; i < len * 2; ++i)
          assertEquals(i < len ? Element(i) : undefined, Read(ta, i));
        assertOptimized(Read);
        blen = Resize(ab, 24);
        FillBuffer(ab);
        len = Math.floor(blen / target.BYTES_PER_ELEMENT);
        for (let i = 0; i < len * 2; ++i)
          assertEquals(i < len ? Element(i) : undefined, Read(ta, i));
        assertOptimized(Read);

        if (!shared) {
          %ArrayBufferDetach(ab);
          assertEquals(undefined, Read(ta, 0));
          //                        assertOptimized(Read);
        }
      }
    }
  }
}
})();

(function() {
for (let shared of [false, true]) {
  for (let length_tracking of [false, true]) {
    for (let with_offset of [false, true]) {
      for (let target
               of [Int8Array, Uint32Array, Float64Array, BigInt64Array]) {
        const test_case = `Testing: Write_${shared ? 'GSAB' : 'RAB'}_${
            length_tracking ? 'LengthTracking' : 'FixedLength'}${
            with_offset ? 'WithOffset' : ''}_${target.name}`;
        // console.log(test_case);

        const write_code = 'ta[index] = value; // ' + test_case;
        const Write = new Function('ta', 'index', 'value', write_code);
        const offset = with_offset ? 8 : 0;

        let blen = 16 - offset;
        let len = Math.floor(blen / target.BYTES_PER_ELEMENT);
        const fixed_len = length_tracking ? undefined : len;
        const ab = CreateBuffer(shared, 16, 40);
        const ta = new target(ab, offset, fixed_len);
        const Resize = MakeResize(target, shared, offset, fixed_len);
        const Element = MakeElement(target, offset);
        const CheckBuffer = MakeCheckBuffer(target, offset);
        ClearBuffer(ab);

        assertUnoptimized(Write);
        %PrepareFunctionForOptimization(Write);
        for (let i = 0; i < len; ++i) {
          Write(ta, i, Element(i));
          CheckBuffer(ab, i);
        }
        ClearBuffer(ab);
        %OptimizeFunctionOnNextCall(Write);
        for (let i = 0; i < len; ++i) {
          Write(ta, i, Element(i));
          CheckBuffer(ab, i);
        }
        assertOptimized(Write);
        blen = Resize(ab, 32);
        ClearBuffer(ab);
        len = Math.floor(blen / target.BYTES_PER_ELEMENT);
        for (let i = 0; i < len; ++i) {
          Write(ta, i, Element(i));
          CheckBuffer(ab, i);
        }
        assertOptimized(Write);
        blen = Resize(ab, 9);
        ClearBuffer(ab);
        len = Math.floor(blen / target.BYTES_PER_ELEMENT);
        for (let i = 0; i < len; ++i) {
          Write(ta, i, Element(i));
          CheckBuffer(ab, i);
        }
        assertOptimized(Write);
        blen = Resize(ab, 24);
        ClearBuffer(ab);
        len = Math.floor(blen / target.BYTES_PER_ELEMENT);
        for (let i = 0; i < len; ++i) {
          Write(ta, i, Element(i));
          CheckBuffer(ab, i);
        }
        assertOptimized(Write);
      }
    }
  }
}
})();

(function() {
for (let shared of [false, true]) {
  for (let length_tracking of [false, true]) {
    for (let with_offset of [false, true]) {
      const test_case = `Testing: ByteLength_${shared ? 'GSAB' : 'RAB'}_${
          length_tracking ?
              'LengthTracking' :
              'FixedLength'}${with_offset ? 'WithOffset' : ''}_DataView`;
      // console.log(test_case);

      const byte_length_code = 'return dv.byteLength; // ' + test_case;
      const ByteLength = new Function('dv', byte_length_code);
      const offset = with_offset ? 8 : 0;

      let blen = 16 - offset;
      const fixed_blen = length_tracking ? undefined : blen;
      const ab = CreateBuffer(shared, 16, 40);
      const dv = new DataView(ab, offset, fixed_blen);
      const Resize = MakeResize(DataView, shared, offset, fixed_blen);

      assertUnoptimized(ByteLength);
      %PrepareFunctionForOptimization(ByteLength);
      assertEquals(blen, ByteLength(dv));
      assertEquals(blen, ByteLength(dv));
      %OptimizeFunctionOnNextCall(ByteLength);
      assertEquals(blen, ByteLength(dv));
      assertOptimized(ByteLength);
      blen = Resize(ab, 32);
      assertEquals(blen, ByteLength(dv));
      assertOptimized(ByteLength);
      blen = Resize(ab, 9);
      if (length_tracking || shared) {
        assertEquals(blen, ByteLength(dv));
      } else {
        // For fixed length rabs, Resize(ab, 9) will put the ArrayBuffer in
        // detached state, for which DataView.prototype.byteLength has to throw.
        assertThrows(() => { ByteLength(dv); }, TypeError);
      }
      assertOptimized(ByteLength);
      blen = Resize(ab, 24);
      assertEquals(blen, ByteLength(dv));
      assertOptimized(ByteLength);

      if (!shared) {
        %ArrayBufferDetach(ab);
        assertThrows(() => { ByteLength(dv); }, TypeError);
        assertOptimized(ByteLength);
      }
    }
  }
}
})();

(function() {
function ByteLength_RAB_LengthTrackingWithOffset_DataView(dv) {
  return dv.byteLength;
}
const ByteLength = ByteLength_RAB_LengthTrackingWithOffset_DataView;

const rab = CreateResizableArrayBuffer(16, 40);
const dv = new DataView(rab, 7);

%PrepareFunctionForOptimization(ByteLength);
assertEquals(9, ByteLength(dv));
assertEquals(9, ByteLength(dv));
%OptimizeFunctionOnNextCall(ByteLength);
assertEquals(9, ByteLength(dv));
assertOptimized(ByteLength);
})();

const dataview_data_sizes = ['Int8', 'Uint8', 'Int16', 'Uint16', 'Int32',
                             'Uint32', 'Float16', 'Float32', 'Float64', 'BigInt64',
                             'BigUint64'];

// Global variable used for DataViews; this is important for triggering some
// optimizations.
var dv;
(function() {
for (let use_global_var of [true, false]) {
  for (let shared of [false, true]) {
    for (let length_tracking of [false, true]) {
      for (let with_offset of [false, true]) {
        for (let data_size of dataview_data_sizes) {
          const test_case = `Testing: Get_${
              data_size}_${
              shared ? 'GSAB' : 'RAB'}_${
              length_tracking ?
                  'LengthTracking' :
                  'FixedLength'}${with_offset ? 'WithOffset' : ''}_${
              use_global_var ? 'UseGlobalVar' : ''}_DataView`;
          // console.log(test_case);
          const is_bigint = data_size.startsWith('Big');
          const expected_value = is_bigint ? 0n : 0;

          const get_code = 'return dv.get' + data_size + '(0); // ' + test_case;
          const Get = use_global_var ?
              new Function(get_code) : new Function('dv', get_code);

          const offset = with_offset ? 8 : 0;

          let blen = 8;  // Enough for one element.
          const fixed_blen = length_tracking ? undefined : blen;
          const ab = CreateBuffer(shared, 8*10, 8*20);
          // Assign to the global var.
          dv = new DataView(ab, offset, fixed_blen);
          const Resize = MakeResize(DataView, shared, offset, fixed_blen);

          assertUnoptimized(Get);
          %PrepareFunctionForOptimization(Get);
          assertEquals(expected_value, Get(dv));
          assertEquals(expected_value, Get(dv));
          %OptimizeFunctionOnNextCall(Get);
          assertEquals(expected_value, Get(dv));
          assertOptimized(Get);

          // Enough for one element or more (even with offset).
          blen = Resize(ab, 8 + offset);
          assertEquals(expected_value, Get(dv));
          assertOptimized(Get);

          blen = Resize(ab, 0);  // Not enough for one element.
          if (shared) {
            assertEquals(expected_value, Get(dv));
          } else {
            if (!length_tracking || with_offset) {
              // DataView is out of bounds.
              assertThrows(() => { Get(dv); }, TypeError);
            } else {
              // DataView is valid, the index is out of bounds.
              assertThrows(() => { Get(dv); }, RangeError);
            }
          }

          blen = Resize(ab, 64);
          assertEquals(expected_value, Get(dv));

          if (!shared) {
            %ArrayBufferDetach(ab);
            assertThrows(() => { Get(dv); }, TypeError);
          }
        }
      }
    }
  }
}
})();

(function() {
function Read_TA_RAB_LengthTracking_Mixed(ta, index) {
  return ta[index];
}
const Get = Read_TA_RAB_LengthTracking_Mixed;

const ab = new ArrayBuffer(16);
FillBuffer(ab);
const rab = CreateResizableArrayBuffer(16, 40);
FillBuffer(rab);
let ta_int8 = new Int8Array(ab);
let ta_uint16 = new Uint16Array(rab);
let ta_float32 = new Float32Array(ab);
let ta_float64 = new Float64Array(rab);

// Train with feedback for all elements kinds.
%PrepareFunctionForOptimization(Get);
assertEquals(0, Get(ta_int8, 0));
assertEquals(3, Get(ta_int8, 3));
assertEquals(15, Get(ta_int8, 15));
assertEquals(undefined, Get(ta_int8, 16));
assertEquals(undefined, Get(ta_int8, 32));
assertEquals(asU16(0), Get(ta_uint16, 0));
assertEquals(asU16(3), Get(ta_uint16, 3));
assertEquals(asU16(7), Get(ta_uint16, 7));
assertEquals(undefined, Get(ta_uint16, 8));
assertEquals(undefined, Get(ta_uint16, 12));
assertEquals(asF32(0), Get(ta_float32, 0));
assertEquals(asF32(3), Get(ta_float32, 3));
assertEquals(undefined, Get(ta_float32, 4));
assertEquals(undefined, Get(ta_float32, 12));
assertEquals(asF64(0), Get(ta_float64, 0));
assertEquals(asF64(1), Get(ta_float64, 1));
assertEquals(undefined, Get(ta_float64, 2));
assertEquals(undefined, Get(ta_float64, 12));
%OptimizeFunctionOnNextCall(Get);
assertEquals(0, Get(ta_int8, 0));
assertEquals(3, Get(ta_int8, 3));
assertEquals(15, Get(ta_int8, 15));
assertEquals(undefined, Get(ta_int8, 16));
assertEquals(undefined, Get(ta_int8, 32));
assertEquals(asU16(0), Get(ta_uint16, 0));
assertEquals(asU16(3), Get(ta_uint16, 3));
assertEquals(asU16(7), Get(ta_uint16, 7));
assertEquals(undefined, Get(ta_uint16, 8));
assertEquals(undefined, Get(ta_uint16, 12));
assertEquals(asF32(0), Get(ta_float32, 0));
assertEquals(asF32(3), Get(ta_float32, 3));
assertEquals(undefined, Get(ta_float32, 4));
assertEquals(undefined, Get(ta_float32, 12));
assertEquals(asF64(0), Get(ta_float64, 0));
assertEquals(asF64(1), Get(ta_float64, 1));
assertEquals(undefined, Get(ta_float64, 2));
assertEquals(undefined, Get(ta_float64, 12));
assertOptimized(Get);
rab.resize(32);
FillBuffer(rab);
assertEquals(0, Get(ta_int8, 0));
assertEquals(3, Get(ta_int8, 3));
assertEquals(15, Get(ta_int8, 15));
assertEquals(undefined, Get(ta_int8, 16));
assertEquals(undefined, Get(ta_int8, 32));
assertEquals(asU16(0), Get(ta_uint16, 0));
assertEquals(asU16(3), Get(ta_uint16, 3));
assertEquals(asU16(15), Get(ta_uint16, 15));
assertEquals(undefined, Get(ta_uint16, 16));
assertEquals(undefined, Get(ta_uint16, 40));
assertEquals(asF32(0), Get(ta_float32, 0));
assertEquals(asF32(3), Get(ta_float32, 3));
assertEquals(undefined, Get(ta_float32, 4));
assertEquals(undefined, Get(ta_float32, 12));
assertEquals(asF64(0), Get(ta_float64, 0));
assertEquals(asF64(1), Get(ta_float64, 1));
assertEquals(asF64(3), Get(ta_float64, 3));
assertEquals(undefined, Get(ta_float64, 4));
assertEquals(undefined, Get(ta_float64, 12));
assertOptimized(Get);
rab.resize(9);
assertEquals(0, Get(ta_int8, 0));
assertEquals(3, Get(ta_int8, 3));
assertEquals(15, Get(ta_int8, 15));
assertEquals(undefined, Get(ta_int8, 16));
assertEquals(undefined, Get(ta_int8, 32));
assertEquals(asU16(0), Get(ta_uint16, 0));
assertEquals(asU16(3), Get(ta_uint16, 3));
assertEquals(undefined, Get(ta_uint16, 4));
assertEquals(undefined, Get(ta_uint16, 12));
assertEquals(asF32(0), Get(ta_float32, 0));
assertEquals(asF32(3), Get(ta_float32, 3));
assertEquals(undefined, Get(ta_float32, 4));
assertEquals(undefined, Get(ta_float32, 12));
assertEquals(asF64(0), Get(ta_float64, 0));
assertEquals(undefined, Get(ta_float64, 1));
assertEquals(undefined, Get(ta_float64, 12));
assertOptimized(Get);

// Call with a different map to trigger deoptimization. We use this
// to verify that we have actually specialized on the above maps only.
let ta_uint8 = new Uint8Array(rab);
assertEquals(7, Get(ta_uint8, 7));
assertUnoptimized(Get);
}());

(function() {
function Read_TA_RAB_LengthTracking_Mixed(ta, index) {
  return ta[index];
}
const Get = Read_TA_RAB_LengthTracking_Mixed;

const ab = new ArrayBuffer(16);
FillBuffer(ab);
const rab = CreateResizableArrayBuffer(16, 40);
FillBuffer(rab);
let ta_int8 = new Int8Array(ab);
let ta_uint16 = new Uint16Array(rab);
let ta_float32 = new Float32Array(ab);
let ta_float64 = new Float64Array(rab);

// Train with feedback for all elements kinds.
%PrepareFunctionForOptimization(Get);
assertEquals(0, Get(ta_int8, 0));
assertEquals(3, Get(ta_int8, 3));
assertEquals(15, Get(ta_int8, 15));
assertEquals(undefined, Get(ta_int8, 16));
assertEquals(undefined, Get(ta_int8, 32));
assertEquals(asU16(0), Get(ta_uint16, 0));
assertEquals(asU16(3), Get(ta_uint16, 3));
assertEquals(asU16(7), Get(ta_uint16, 7));
assertEquals(undefined, Get(ta_uint16, 8));
assertEquals(undefined, Get(ta_uint16, 12));
assertEquals(asF32(0), Get(ta_float32, 0));
assertEquals(asF32(3), Get(ta_float32, 3));
assertEquals(undefined, Get(ta_float32, 4));
assertEquals(undefined, Get(ta_float32, 12));
assertEquals(asF64(0), Get(ta_float64, 0));
assertEquals(asF64(1), Get(ta_float64, 1));
assertEquals(undefined, Get(ta_float64, 2));
assertEquals(undefined, Get(ta_float64, 12));
%OptimizeFunctionOnNextCall(Get);
assertEquals(0, Get(ta_int8, 0));
assertEquals(3, Get(ta_int8, 3));
assertEquals(15, Get(ta_int8, 15));
assertEquals(undefined, Get(ta_int8, 16));
assertEquals(undefined, Get(ta_int8, 32));
assertEquals(asU16(0), Get(ta_uint16, 0));
assertEquals(asU16(3), Get(ta_uint16, 3));
assertEquals(asU16(7), Get(ta_uint16, 7));
assertEquals(undefined, Get(ta_uint16, 8));
assertEquals(undefined, Get(ta_uint16, 12));
assertEquals(asF32(0), Get(ta_float32, 0));
assertEquals(asF32(3), Get(ta_float32, 3));
assertEquals(undefined, Get(ta_float32, 4));
assertEquals(undefined, Get(ta_float32, 12));
assertEquals(asF64(0), Get(ta_float64, 0));
assertEquals(asF64(1), Get(ta_float64, 1));
assertEquals(undefined, Get(ta_float64, 2));
assertEquals(undefined, Get(ta_float64, 12));
assertOptimized(Get);
rab.resize(32);
FillBuffer(rab);
assertEquals(0, Get(ta_int8, 0));
assertEquals(3, Get(ta_int8, 3));
assertEquals(15, Get(ta_int8, 15));
assertEquals(undefined, Get(ta_int8, 16));
assertEquals(undefined, Get(ta_int8, 32));
assertEquals(asU16(0), Get(ta_uint16, 0));
assertEquals(asU16(3), Get(ta_uint16, 3));
assertEquals(asU16(15), Get(ta_uint16, 15));
assertEquals(undefined, Get(ta_uint16, 16));
assertEquals(undefined, Get(ta_uint16, 40));
assertEquals(asF32(0), Get(ta_float32, 0));
assertEquals(asF32(3), Get(ta_float32, 3));
assertEquals(undefined, Get(ta_float32, 4));
assertEquals(undefined, Get(ta_float32, 12));
assertEquals(asF64(0), Get(ta_float64, 0));
assertEquals(asF64(1), Get(ta_float64, 1));
assertEquals(asF64(3), Get(ta_float64, 3));
assertEquals(undefined, Get(ta_float64, 4));
assertEquals(undefined, Get(ta_float64, 12));
assertOptimized(Get);
rab.resize(9);
assertEquals(0, Get(ta_int8, 0));
assertEquals(3, Get(ta_int8, 3));
assertEquals(15, Get(ta_int8, 15));
assertEquals(undefined, Get(ta_int8, 16));
assertEquals(undefined, Get(ta_int8, 32));
assertEquals(asU16(0), Get(ta_uint16, 0));
assertEquals(asU16(3), Get(ta_uint16, 3));
assertEquals(undefined, Get(ta_uint16, 4));
assertEquals(undefined, Get(ta_uint16, 12));
assertEquals(asF32(0), Get(ta_float32, 0));
assertEquals(asF32(3), Get(ta_float32, 3));
assertEquals(undefined, Get(ta_float32, 4));
assertEquals(undefined, Get(ta_float32, 12));
assertEquals(asF64(0), Get(ta_float64, 0));
assertEquals(undefined, Get(ta_float64, 1));
assertEquals(undefined, Get(ta_float64, 12));
assertOptimized(Get);

// Call with a different map to trigger deoptimization. We use this
// to verify that we have actually specialized on the above maps only.
let ta_uint8 = new Uint8Array(rab);
Get(7, Get(ta_uint8, 7));
assertUnoptimized(Get);
}());

(function() {
function Length_TA_RAB_LengthTracking_Mixed(ta) {
  return ta.length;
}
let Length = Length_TA_RAB_LengthTracking_Mixed;

const ab = new ArrayBuffer(32);
const rab = CreateResizableArrayBuffer(16, 40);
let ta_int8 = new Int8Array(ab);
let ta_uint16 = new Uint16Array(rab);
let ta_float32 = new Float32Array(ab);
let ta_bigint64 = new BigInt64Array(rab);

// Train with feedback for all elements kinds.
%PrepareFunctionForOptimization(Length);
assertEquals(32, Length(ta_int8));
assertEquals(8, Length(ta_uint16));
assertEquals(8, Length(ta_float32));
assertEquals(2, Length(ta_bigint64));
%OptimizeFunctionOnNextCall(Length);
assertEquals(32, Length(ta_int8));
assertEquals(8, Length(ta_uint16));
assertEquals(8, Length(ta_float32));
assertEquals(2, Length(ta_bigint64));
assertOptimized(Length);
}());

(function() {
function Length_RAB_GSAB_LengthTrackingWithOffset_Mixed(ta) {
  return ta.length;
}
const Length = Length_RAB_GSAB_LengthTrackingWithOffset_Mixed;

const rab = CreateResizableArrayBuffer(16, 40);
let ta_int8 = new Int8Array(rab);
let ta_float64 = new Float64Array(rab);

// Train with feedback for Int8Array and Float64Array.
%PrepareFunctionForOptimization(Length);
assertEquals(16, Length(ta_int8));
assertEquals(2, Length(ta_float64));
%OptimizeFunctionOnNextCall(Length);
assertEquals(16, Length(ta_int8));
assertEquals(2, Length(ta_float64));
assertOptimized(Length);

let ta_uint32 = new Uint32Array(rab);
let ta_bigint64 = new BigInt64Array(rab);
// Calling with Uint32Array will deopt because of the map check on length.
assertEquals(4, Length(ta_uint32));
assertUnoptimized(Length);
%PrepareFunctionForOptimization(Length);
assertEquals(2, Length(ta_bigint64));
// Recompile with additional feedback for Uint32Array and BigInt64Array.
%OptimizeFunctionOnNextCall(Length);
assertEquals(2, Length(ta_bigint64));
assertOptimized(Length);

// Length handles all four TypedArrays without deopting.
assertEquals(16, Length(ta_int8));
assertEquals(2, Length(ta_float64));
assertEquals(4, Length(ta_uint32));
assertEquals(2, Length(ta_bigint64));
assertOptimized(Length);

// Length handles corresponding gsab-backed TypedArrays without deopting.
const gsab = CreateGrowableSharedArrayBuffer(16, 40);
let ta2_uint32 = new Uint32Array(gsab, 8);
let ta2_float64 = new Float64Array(gsab, 8);
let ta2_bigint64 = new BigInt64Array(gsab, 8);
let ta2_int8 = new Int8Array(gsab, 8);
assertEquals(8, Length(ta2_int8));
assertEquals(1, Length(ta2_float64));
assertEquals(2, Length(ta2_uint32));
assertEquals(1, Length(ta2_bigint64));
assertOptimized(Length);

// Test Length after rab has been resized to a smaller size.
rab.resize(5);
assertEquals(5, Length(ta_int8));
assertEquals(0, Length(ta_float64));
assertEquals(1, Length(ta_uint32));
assertEquals(0, Length(ta_bigint64));
assertOptimized(Length);

// Test Length after rab has been resized to a larger size.
rab.resize(40);
assertEquals(40, Length(ta_int8));
assertEquals(5, Length(ta_float64));
assertEquals(10, Length(ta_uint32));
assertEquals(5, Length(ta_bigint64));
assertOptimized(Length);

// Test Length after gsab has been grown to a larger size.
gsab.grow(25);
assertEquals(17, Length(ta2_int8));
assertEquals(2, Length(ta2_float64));
assertEquals(4, Length(ta2_uint32));
assertEquals(2, Length(ta2_bigint64));
assertOptimized(Length);
})();

(function() {
function Length_AB_RAB_GSAB_LengthTrackingWithOffset_Mixed(ta) {
  return ta.length;
}
const Length = Length_AB_RAB_GSAB_LengthTrackingWithOffset_Mixed;

let ab = new ArrayBuffer(32);
let rab = CreateResizableArrayBuffer(16, 40);
let gsab = CreateGrowableSharedArrayBuffer(16, 40);

let ta_ab_int32 = new Int32Array(ab, 8, 3);
let ta_rab_int32 = new Int32Array(rab, 4);
let ta_gsab_float64 = new Float64Array(gsab);
let ta_gsab_bigint64 = new BigInt64Array(gsab, 0, 2);

// Optimize Length with polymorphic feedback.
%PrepareFunctionForOptimization(Length);
assertEquals(3, Length(ta_ab_int32));
assertEquals(3, Length(ta_rab_int32));
assertEquals(2, Length(ta_gsab_float64));
assertEquals(2, Length(ta_gsab_bigint64));
%OptimizeFunctionOnNextCall(Length);
assertEquals(3, Length(ta_ab_int32));
assertEquals(3, Length(ta_rab_int32));
assertEquals(2, Length(ta_gsab_float64));
assertEquals(2, Length(ta_gsab_bigint64));
assertOptimized(Length);

// Test resizing and growing the underlying rab/gsab buffers.
rab.resize(8);
gsab.grow(36);
assertEquals(3, Length(ta_ab_int32));
assertEquals(1, Length(ta_rab_int32));
assertEquals(4, Length(ta_gsab_float64));
assertEquals(2, Length(ta_gsab_bigint64));
assertOptimized(Length);

// Construct additional TypedArrays with the same ElementsKind.
let ta2_ab_bigint64 = new BigInt64Array(ab, 0, 1);
let ta2_gsab_int32 = new Int32Array(gsab, 16);
let ta2_rab_float64 = new Float64Array(rab, 8);
let ta2_rab_int32 = new Int32Array(rab, 0, 1);
assertEquals(1, Length(ta2_ab_bigint64));
assertEquals(5, Length(ta2_gsab_int32));
assertEquals(0, Length(ta2_rab_float64));
assertEquals(1, Length(ta2_rab_int32));
assertOptimized(Length);
})();

(function() {
function ByteOffset(ta) {
  return ta.byteOffset;
}

const rab = CreateResizableArrayBuffer(16, 40);
const ta = new Int32Array(rab, 4);

%PrepareFunctionForOptimization(ByteOffset);
assertEquals(4, ByteOffset(ta));
assertEquals(4, ByteOffset(ta));
%OptimizeFunctionOnNextCall(ByteOffset);
assertEquals(4, ByteOffset(ta));
assertOptimized(ByteOffset);
})();
