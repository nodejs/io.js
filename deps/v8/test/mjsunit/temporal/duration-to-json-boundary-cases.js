// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

// Test Temporal.Duration toJSON with extreme cases.
//
// Test Number.MAX_VALUE
//
// This is out of the range of Number.MAX_SAFE_INTEGER so the specification
// does not mandate the precision. But we should still check certain property of
// the result.
// Number.MAX_VALUE is 1.7976931348623157e+308 so the first 16 characters should
// be "P179769313486231" which is 15 digits and only require 50 bits so that
// should be precious in 64 bit floating point.
// There are total 309 digits so it should be 179769313486231 with another
// 294 digits (309-15 = 294)
let MAX_UINT32 = Math.pow(2,32);
assertThrows(() => (new Temporal.Duration(MAX_UINT32)), RangeError);
assertThrows(() => (new Temporal.Duration(-MAX_UINT32)), RangeError);
assertThrows(() => (new Temporal.Duration(0, MAX_UINT32)), RangeError);
assertThrows(() => (new Temporal.Duration(0, -MAX_UINT32)), RangeError);
assertThrows(() => (new Temporal.Duration(0, 0, MAX_UINT32)), RangeError);
assertThrows(() => (new Temporal.Duration(0, 0, -MAX_UINT32)), RangeError);
assertThrows(() => (new Temporal.Duration(0, 0, 0, (Number.MAX_SAFE_INTEGER/86400 +1))), RangeError);
assertEquals("P" + Math.floor(Number.MAX_SAFE_INTEGER/86400) +"D",
    (new Temporal.Duration(0, 0, 0, Math.floor(Number.MAX_SAFE_INTEGER/86400))).toJSON());
assertThrows(() => (new Temporal.Duration(0, 0, 0, -(Number.MAX_SAFE_INTEGER/86400 +1))), RangeError);
assertEquals("-P" + Math.floor(Number.MAX_SAFE_INTEGER/86400) +"D",
    (new Temporal.Duration(0, 0, 0, -Math.floor(Number.MAX_SAFE_INTEGER/86400))).toJSON());
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, (Number.MAX_SAFE_INTEGER/3600 +1))), RangeError);
assertEquals("PT" + Math.floor(Number.MAX_SAFE_INTEGER/3600) +"H",
    (new Temporal.Duration(0, 0, 0, 0, Math.floor(Number.MAX_SAFE_INTEGER/3600))).toJSON());
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, -(Number.MAX_SAFE_INTEGER/3600 +1))), RangeError);
assertEquals("-PT" + Math.floor(Number.MAX_SAFE_INTEGER/3600) +"H",
    (new Temporal.Duration(0, 0, 0, 0, -Math.floor(Number.MAX_SAFE_INTEGER/3600))).toJSON());
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, 0, (Number.MAX_SAFE_INTEGER/60 +1))), RangeError);
assertEquals("PT" + Math.floor(Number.MAX_SAFE_INTEGER/60) +"M",
    (new Temporal.Duration(0, 0, 0, 0, 0, Math.floor(Number.MAX_SAFE_INTEGER/60))).toJSON());
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, 0, -(Number.MAX_SAFE_INTEGER/60 +1))), RangeError);
assertEquals("-PT" + Math.floor(Number.MAX_SAFE_INTEGER/60) +"M",
    (new Temporal.Duration(0, 0, 0, 0, 0, -Math.floor(Number.MAX_SAFE_INTEGER/60))).toJSON());
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, 0, 0, Number.MAX_SAFE_INTEGER+1)), RangeError);
assertEquals("PT" + Number.MAX_SAFE_INTEGER +"S",
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, Number.MAX_SAFE_INTEGER)).toJSON());
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, 0, 0, -(Number.MAX_SAFE_INTEGER+1))), RangeError);
assertEquals("-PT" + Number.MAX_SAFE_INTEGER +"S",
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, -Number.MAX_SAFE_INTEGER)).toJSON());
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, 0, 0, Number.MAX_SAFE_INTEGER, 1000)), RangeError);
assertEquals("PT" + Number.MAX_SAFE_INTEGER +".999S",
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, Number.MAX_SAFE_INTEGER, 999)).toJSON());
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, 0, 0, -Number.MAX_SAFE_INTEGER, -1000)), RangeError);
assertEquals("-PT" + Number.MAX_SAFE_INTEGER +".999S",
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, -Number.MAX_SAFE_INTEGER, -999)).toJSON());
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, 0, 0, Number.MAX_SAFE_INTEGER, 999, 1000)), RangeError);
assertEquals("PT" + Number.MAX_SAFE_INTEGER +".999999S",
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, Number.MAX_SAFE_INTEGER, 999, 999)).toJSON());
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, 0, 0, -Number.MAX_SAFE_INTEGER, -999, -1000)), RangeError);
assertEquals("-PT" + Number.MAX_SAFE_INTEGER +".999999S",
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, -Number.MAX_SAFE_INTEGER, -999, -999)).toJSON());
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, 0, 0, Number.MAX_SAFE_INTEGER, 999, 999, 1000)), RangeError);
assertEquals("PT" + Number.MAX_SAFE_INTEGER +".999999999S",
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, Number.MAX_SAFE_INTEGER, 999, 999, 999)).toJSON());
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, 0, 0, -Number.MAX_SAFE_INTEGER, -999, -999, -1000)), RangeError);
assertEquals("-PT" + Number.MAX_SAFE_INTEGER +".999999999S",
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, -Number.MAX_SAFE_INTEGER, -999, -999, -999)).toJSON());

// Put Number.MAX_SAFE_INTEGER in ms
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, 0, 0, // y, m, wk, d, h, m
                           Number.MAX_SAFE_INTEGER - Math.floor(Number.MAX_SAFE_INTEGER/1e3) + 1, // s
                           Number.MAX_SAFE_INTEGER, // ms
                           0, // mic
                           0 // ns
                           )), RangeError);
assertEquals("PT" + Number.MAX_SAFE_INTEGER + "." + (Number.MAX_SAFE_INTEGER % 1000) + "S",
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, // y, m, wk, d, h, m
                           Number.MAX_SAFE_INTEGER - Math.floor(Number.MAX_SAFE_INTEGER/1e3), // s
                           Number.MAX_SAFE_INTEGER, // ms
                           0, // mic
                           0 // ns
                           )).toJSON());

assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, 0, 0, // y, m, wk, d, h, m
                           -(Number.MAX_SAFE_INTEGER - Math.floor(Number.MAX_SAFE_INTEGER/1e3) + 1), // s
                           -Number.MAX_SAFE_INTEGER, // ms
                           0, // mic
                           0 // ns
                           )), RangeError);
assertEquals("-PT" + Number.MAX_SAFE_INTEGER + "." + (Number.MAX_SAFE_INTEGER % 1000) + "S",
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, // y, m, wk, d, h, m
                           -(Number.MAX_SAFE_INTEGER - Math.floor(Number.MAX_SAFE_INTEGER/1e3)), // s
                           -Number.MAX_SAFE_INTEGER, // ms
                           0, // mic
                           0 // ns
                           )).toJSON());

// Put Number.MAX_SAFE_INTEGER in mic
assertThrows(() => (new Temporal.Duration(0, 0, 0, 0, 0, 0, // y, m, wk, d, h, m
                           Number.MAX_SAFE_INTEGER - Math.floor(Number.MAX_SAFE_INTEGER/1e6) + 1, // s
                           0,  // ms
                           Number.MAX_SAFE_INTEGER, // mic
                           0 // ns
                           )), RangeError);
assertEquals("PT" + Number.MAX_SAFE_INTEGER + "." + (Number.MAX_SAFE_INTEGER % 1000000) + "S",
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, // y, m, wk, d, h, m
                           Number.MAX_SAFE_INTEGER - Math.floor(Number.MAX_SAFE_INTEGER/1e6), // s
                           0, // ms
                           Number.MAX_SAFE_INTEGER, // mic
                           0 // ns
                           )).toJSON());


assertEquals("P" + (MAX_UINT32-1) + "Y", (new Temporal.Duration(MAX_UINT32-1)).toJSON());
assertEquals("-P" + (MAX_UINT32-1) + "Y", (new Temporal.Duration(-(MAX_UINT32-1))).toJSON());
assertEquals("P" + (MAX_UINT32-1) + "M", (new Temporal.Duration(0, MAX_UINT32-1)).toJSON());
assertEquals("-P" + (MAX_UINT32-1) + "M", (new Temporal.Duration(0, -(MAX_UINT32-1))).toJSON());
assertEquals("P" + (MAX_UINT32-1) + "W", (new Temporal.Duration(0, 0, MAX_UINT32-1)).toJSON());
assertEquals("-P" + (MAX_UINT32-1) + "W", (new Temporal.Duration(0, 0, -(MAX_UINT32-1))).toJSON());
assertEquals("P" + (MAX_UINT32-1) + "D", (new Temporal.Duration(0, 0, 0, MAX_UINT32-1)).toJSON());
assertEquals("-P" + (MAX_UINT32-1) + "D", (new Temporal.Duration(0, 0, 0, -(MAX_UINT32-1))).toJSON());
assertEquals("PT" + (MAX_UINT32-1) + "H", (new Temporal.Duration(0, 0, 0, 0, MAX_UINT32-1)).toJSON());
assertEquals("-PT" + (MAX_UINT32-1) + "H", (new Temporal.Duration(0, 0, 0, 0, -(MAX_UINT32-1))).toJSON());
assertEquals("PT" + (MAX_UINT32-1) + "M", (new Temporal.Duration(0, 0, 0, 0, 0, MAX_UINT32-1)).toJSON());
assertEquals("-PT" + (MAX_UINT32-1) + "M", (new Temporal.Duration(0, 0, 0, 0, 0, -(MAX_UINT32-1))).toJSON());
assertEquals("PT" + (MAX_UINT32-1) + "S", (new Temporal.Duration(0, 0, 0, 0, 0, 0, MAX_UINT32-1)).toJSON());
assertEquals("-PT" + (MAX_UINT32-1) + "S", (new Temporal.Duration(0, 0, 0, 0, 0, 0, -(MAX_UINT32-1))).toJSON());
