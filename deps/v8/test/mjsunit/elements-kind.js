// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Flags: --allow-natives-syntax --expose-gc
// Flags: --deopt-every-n-times=0
// Flags: --js-float16array

var elements_kind = {
  fast_smi_only             :  'fast smi only elements',
  fast                      :  'fast elements',
  fast_double               :  'fast double elements',
  dictionary                :  'dictionary elements',
  fixed_int32               :  'fixed int8 elements',
  fixed_uint8               :  'fixed uint8 elements',
  fixed_int16               :  'fixed int16 elements',
  fixed_uint16              :  'fixed uint16 elements',
  fixed_int32               :  'fixed int32 elements',
  fixed_uint32              :  'fixed uint32 elements',
  fixed_float16             :  'fixed float16 elements',
  fixed_float32             :  'fixed float32 elements',
  fixed_float64             :  'fixed float64 elements',
  fixed_uint8_clamped       :  'fixed uint8_clamped elements'
}

function getKind(obj) {
  if (%HasSmiElements(obj)) return elements_kind.fast_smi_only;
  if (%HasObjectElements(obj)) return elements_kind.fast;
  if (%HasDoubleElements(obj)) return elements_kind.fast_double;
  if (%HasDictionaryElements(obj)) return elements_kind.dictionary;

  if (%HasFixedInt8Elements(obj)) {
    return elements_kind.fixed_int8;
  }
  if (%HasFixedUint8Elements(obj)) {
    return elements_kind.fixed_uint8;
  }
  if (%HasFixedInt16Elements(obj)) {
    return elements_kind.fixed_int16;
  }
  if (%HasFixedUint16Elements(obj)) {
    return elements_kind.fixed_uint16;
  }
  if (%HasFixedInt32Elements(obj)) {
    return elements_kind.fixed_int32;
  }
  if (%HasFixedUint32Elements(obj)) {
    return elements_kind.fixed_uint32;
  }
  if (%HasFixedFloat16Elements(obj)) {
    return elements_kind.fixed_float16;
  }
  if (%HasFixedFloat32Elements(obj)) {
    return elements_kind.fixed_float32;
  }
  if (%HasFixedFloat64Elements(obj)) {
    return elements_kind.fixed_float64;
  }
  if (%HasFixedUint8ClampedElements(obj)) {
    return elements_kind.fixed_uint8_clamped;
  }
}

function assertKind(expected, obj, name_opt) {
  assertEquals(expected, getKind(obj), name_opt);
}

var me = {};
assertKind(elements_kind.fast, me);
me.dance = 0xD15C0;
me.drink = 0xC0C0A;
assertKind(elements_kind.fast, me);

var too = [1,2,3];
assertKind(elements_kind.fast_smi_only, too);
too.dance = 0xD15C0;
too.drink = 0xC0C0A;
assertKind(elements_kind.fast_smi_only, too);

// Make sure the element kind transitions from smi when a non-smi is stored.
function test_wrapper() {
  var you = new Array();
  assertKind(elements_kind.fast_smi_only, you);
  for (var i = 0; i < 1337; i++) {
    var val = i;
    if (i == 1336) {
      assertKind(elements_kind.fast_smi_only, you);
      val = new Object();
    }
    you[i] = val;
  }
  assertKind(elements_kind.fast, you);

  var temp = [];
  // If we store beyond kMaxGap (1024) we should transition to slow elements.
  temp[1024] = 0;
  assertKind(elements_kind.dictionary, temp);

  var fast_double_array = new Array(0xDECAF);
  // If the gap is greater than 1024 (kMaxGap) we would transition the array
  // to slow. So increment should be less than 1024.
  for (var i = 0; i < 0xDECAF; i+=1023) {
    fast_double_array[i] = i / 2;
  }
  assertKind(elements_kind.fast_double, fast_double_array);

  assertKind(elements_kind.fixed_int8,    new Int8Array(007));
  assertKind(elements_kind.fixed_uint8,   new Uint8Array(007));
  assertKind(elements_kind.fixed_int16,   new Int16Array(666));
  assertKind(elements_kind.fixed_uint16,  new Uint16Array(42));
  assertKind(elements_kind.fixed_int32,   new Int32Array(0xF));
  assertKind(elements_kind.fixed_uint32,  new Uint32Array(23));
  assertKind(elements_kind.fixed_float16, new Float16Array(7));
  assertKind(elements_kind.fixed_float32, new Float32Array(7));
  assertKind(elements_kind.fixed_float64, new Float64Array(0));
  assertKind(elements_kind.fixed_uint8_clamped, new Uint8ClampedArray(512));

  var ab = new ArrayBuffer(128);
  assertKind(elements_kind.fixed_int8,    new Int8Array(ab));
  assertKind(elements_kind.fixed_uint8,   new Uint8Array(ab));
  assertKind(elements_kind.fixed_int16,   new Int16Array(ab));
  assertKind(elements_kind.fixed_uint16,  new Uint16Array(ab));
  assertKind(elements_kind.fixed_int32,   new Int32Array(ab));
  assertKind(elements_kind.fixed_uint32,  new Uint32Array(ab));
  assertKind(elements_kind.fixed_float16, new Float16Array(ab));
  assertKind(elements_kind.fixed_float32, new Float32Array(ab));
  assertKind(elements_kind.fixed_float64, new Float64Array(ab));
  assertKind(elements_kind.fixed_uint8_clamped, new Uint8ClampedArray(ab));

  // Crankshaft support for smi-only array elements.
  function monomorphic(array) {
    assertKind(elements_kind.fast_smi_only, array);
    for (var i = 0; i < 3; i++) {
      array[i] = i + 10;
    }
    assertKind(elements_kind.fast_smi_only, array);
    for (var i = 0; i < 3; i++) {
      var a = array[i];
      assertEquals(i + 10, a);
    }
  }
  var smi_only = new Array(1, 2, 3);
  assertKind(elements_kind.fast_smi_only, smi_only);
  %PrepareFunctionForOptimization(monomorphic);
  for (var i = 0; i < 3; i++) monomorphic(smi_only);
    %OptimizeFunctionOnNextCall(monomorphic);
  monomorphic(smi_only);
}

// The test is called in a wrapper function to eliminate the transition learning
// feedback of AllocationSites.
test_wrapper();
%ClearFunctionFeedback(test_wrapper);

%NeverOptimizeFunction(construct_smis);

// This code exists to eliminate the learning influence of AllocationSites
// on the following tests.
var __sequence = 0;
function make_array_string() {
  this.__sequence = this.__sequence + 1;
  return "/* " + this.__sequence + " */  [0, 0, 0];"
}
function make_array() {
  return eval(make_array_string());
}

%EnsureFeedbackVectorForFunction(construct_smis);
function construct_smis() {
  var a = make_array();
  a[0] = 0;  // Send the COW array map to the steak house.
  assertKind(elements_kind.fast_smi_only, a);
  return a;
}

  %NeverOptimizeFunction(construct_doubles);
%EnsureFeedbackVectorForFunction(construct_doubles);
function construct_doubles() {
  var a = construct_smis();
  a[0] = 1.5;
  assertKind(elements_kind.fast_double, a);
  return a;
}

  %NeverOptimizeFunction(construct_objects);
%EnsureFeedbackVectorForFunction(construct_objects);
function construct_objects() {
  var a = construct_smis();
  a[0] = "one";
  assertKind(elements_kind.fast, a);
  return a;
}

// Test crankshafted transition SMI->DOUBLE.
  %EnsureFeedbackVectorForFunction(convert_to_double);
function convert_to_double(array) {
  array[1] = 2.5;
  assertKind(elements_kind.fast_double, array);
  assertEquals(2.5, array[1]);
};
%PrepareFunctionForOptimization(convert_to_double);
var smis = construct_smis();
for (var i = 0; i < 3; i++) convert_to_double(smis);
  %OptimizeFunctionOnNextCall(convert_to_double);
smis = construct_smis();
convert_to_double(smis);
// Test crankshafted transitions SMI->FAST and DOUBLE->FAST.
  %EnsureFeedbackVectorForFunction(convert_to_fast);
function convert_to_fast(array) {
  array[1] = "two";
  assertKind(elements_kind.fast, array);
  assertEquals("two", array[1]);
};
%PrepareFunctionForOptimization(convert_to_fast);
smis = construct_smis();
for (var i = 0; i < 3; i++) convert_to_fast(smis);
var doubles = construct_doubles();
for (var i = 0; i < 3; i++) convert_to_fast(doubles);
smis = construct_smis();
doubles = construct_doubles();
  %OptimizeFunctionOnNextCall(convert_to_fast);
convert_to_fast(smis);
convert_to_fast(doubles);
// Test transition chain SMI->DOUBLE->FAST (crankshafted function will
// transition to FAST directly).
function convert_mixed(array, value, kind) {
  array[1] = value;
  assertKind(kind, array);
  assertEquals(value, array[1]);
}
%PrepareFunctionForOptimization(convert_mixed);
smis = construct_smis();
for (var i = 0; i < 3; i++) {
  convert_mixed(smis, 1.5, elements_kind.fast_double);
}
doubles = construct_doubles();
for (var i = 0; i < 3; i++) {
  convert_mixed(doubles, "three", elements_kind.fast);
}
convert_mixed(construct_smis(), "three", elements_kind.fast);
convert_mixed(construct_doubles(), "three", elements_kind.fast);

if (%ICsAreEnabled()) {
  // Test that allocation sites allocate correct elements kind initially based
  // on previous transitions.
  %OptimizeFunctionOnNextCall(convert_mixed);
  smis = construct_smis();
  doubles = construct_doubles();
  convert_mixed(smis, 1, elements_kind.fast);
  convert_mixed(doubles, 1, elements_kind.fast);
  assertTrue(%HaveSameMap(smis, doubles));
}

// Crankshaft support for smi-only elements in dynamic array literals.
function get(foo) { return foo; }  // Used to generate dynamic values.

function crankshaft_test() {
  var a1 = [get(1), get(2), get(3)];
  assertKind(elements_kind.fast_smi_only, a1);

  var a2 = new Array(get(1), get(2), get(3));
  assertKind(elements_kind.fast_smi_only, a2);
  var b = [get(1), get(2), get("three")];
  assertKind(elements_kind.fast, b);
  var c = [get(1), get(2), get(3.5)];
  assertKind(elements_kind.fast_double, c);
}
%PrepareFunctionForOptimization(crankshaft_test);
for (var i = 0; i < 3; i++) {
  crankshaft_test();
}
%OptimizeFunctionOnNextCall(crankshaft_test);
crankshaft_test();

// Elements_kind transitions for arrays.

// A map can have three different elements_kind transitions: SMI->DOUBLE,
// DOUBLE->OBJECT, and SMI->OBJECT. No matter in which order these three are
// created, they must always end up with the same FAST map.

// Preparation: create one pair of identical objects for each case.
var a = [1, 2, 3];
var b = [1, 2, 3];
assertTrue(%HaveSameMap(a, b));
assertKind(elements_kind.fast_smi_only, a);
var c = [1, 2, 3];
c["case2"] = true;
var d = [1, 2, 3];
d["case2"] = true;
assertTrue(%HaveSameMap(c, d));
assertFalse(%HaveSameMap(a, c));
assertKind(elements_kind.fast_smi_only, c);
var e = [1, 2, 3];
e["case3"] = true;
var f = [1, 2, 3];
f["case3"] = true;
assertTrue(%HaveSameMap(e, f));
assertFalse(%HaveSameMap(a, e));
assertFalse(%HaveSameMap(c, e));
assertKind(elements_kind.fast_smi_only, e);
// Case 1: SMI->DOUBLE, DOUBLE->OBJECT, SMI->OBJECT.
a[0] = 1.5;
assertKind(elements_kind.fast_double, a);
a[0] = "foo";
assertKind(elements_kind.fast, a);
b[0] = "bar";
assertTrue(%HaveSameMap(a, b));
// Case 2: SMI->DOUBLE, SMI->OBJECT, DOUBLE->OBJECT.
c[0] = 1.5;
assertKind(elements_kind.fast_double, c);
assertFalse(%HaveSameMap(c, d));
d[0] = "foo";
assertKind(elements_kind.fast, d);
assertFalse(%HaveSameMap(c, d));
c[0] = "bar";
assertTrue(%HaveSameMap(c, d));
// Case 3: SMI->OBJECT, SMI->DOUBLE, DOUBLE->OBJECT.
e[0] = "foo";
assertKind(elements_kind.fast, e);
assertFalse(%HaveSameMap(e, f));
f[0] = 1.5;
assertKind(elements_kind.fast_double, f);
assertFalse(%HaveSameMap(e, f));
f[0] = "bar";
assertKind(elements_kind.fast, f);
assertTrue(%HaveSameMap(e, f));

// Test if Array.concat() works correctly with DOUBLE elements.
var a = [1, 2];
assertKind(elements_kind.fast_smi_only, a);
var b = [4.5, 5.5];
assertKind(elements_kind.fast_double, b);
var c = a.concat(b);
assertEquals([1, 2, 4.5, 5.5], c);
assertKind(elements_kind.fast_double, c);

// Test that Array.push() correctly handles SMI elements.
var a = [1, 2];
assertKind(elements_kind.fast_smi_only, a);
a.push(3, 4, 5);
assertKind(elements_kind.fast_smi_only, a);
assertEquals([1, 2, 3, 4, 5], a);

// Test that Array.splice() and Array.slice() return correct ElementsKinds.
var a = ["foo", "bar"];
assertKind(elements_kind.fast, a);
var b = a.splice(0, 1);
assertKind(elements_kind.fast, b);
var c = a.slice(0, 1);
assertKind(elements_kind.fast, c);

// Throw away type information in the ICs for next stress run.
gc();
