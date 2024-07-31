// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --allow-natives-syntax

// Tests megamorphic case
function clone1(o) {
  return {...o};
}
// Tests monomorphic case
function clone2(o) {
  return {...o};
}
// Tests null proto
function clone3(o) {
  return {...o, __proto__: null};
}
%PrepareFunctionForOptimization(clone1);
%PrepareFunctionForOptimization(clone2);
%PrepareFunctionForOptimization(clone3);

function test(a, b) {
  %ClearFunctionFeedback(clone2);
  assertEquals({...b}, a);
  assertEquals(clone1(b), a);
  assertEquals(clone1(b), a);
  assertEquals(clone2(b), a);
  assertEquals(clone2(b), a);
  assertEquals(clone1(b).constructor, a.constructor);
  assertEquals(clone2(b).constructor, a.constructor);
  %ClearFunctionFeedback(clone2);
  assertEquals(clone2(b).constructor, a.constructor);

  var a2 = {...a};
  // Provoke some transitions
  %ClearFunctionFeedback(clone2);
  Object.assign(a, {xx: 42})
  assertEquals({...b, xx: 42}, a);
  assertEquals({...clone1(b), xx: 42}, a);
  assertEquals({...clone1(b), xx: 42}, a);
  assertEquals({...clone2(b), xx: 42}, a);
  assertEquals({...clone2(b), xx: 42}, a);

  %ClearFunctionFeedback(clone2);
  Object.assign(a, {xx: 42.2})
  assertEquals({...b, xx: 42.2}, a);
  assertEquals({...clone1(b), xx: 42.2}, a);
  assertEquals({...clone1(b), xx: 42.2}, a);
  assertEquals({...clone2(b), xx: 42.2}, a);
  assertEquals({...clone2(b), xx: 42.2}, a);

  %ClearFunctionFeedback(clone3);
  a2.__proto__ = null;
  assertEquals(clone3(b), a2);
  assertEquals(clone3(b), a2);
  assertEquals(clone3(b).__proto__, undefined);
}

test({});
test({}, false);
test({}, 1.1);
test({}, NaN);
test({}, 2);
test({}, new function(){});
test({}, test);
test({}, {}.__proto__);
test({}, new Proxy({}, function(){}));
test({a: "a"}, new Proxy({a: "a"}, function(){}));
test({}, BigInt(2));
test({}, Symbol("ab"));
test({0: "a", 1: "b"}, "ab");

// non-enumerable
var obj = {a: 1}
Object.defineProperty(obj, 'b', {
  value: 42,
});
test({a: 1}, obj);
assertFalse(%HaveSameMap({...obj},obj));

// some not writable
var obj = {}
Object.defineProperty(obj, 'a', {
  value: 42,
  writable: false,
  enumerable: true
});
obj.b = 1;
test({a: 42, b: 1}, obj);
assertFalse(%HaveSameMap({...obj},obj));

// non-enumerable after non-writable
var obj = {}
Object.defineProperty(obj, 'a', {
  value: 1,
  writable: false,
  enumerable: true,
});
Object.defineProperty(obj, 'b', {
  value: 2,
});
test({a: 1}, obj);
var c = {...obj, a: 4};

test({0:1,1:2}, [1,2]);

var buffer = new ArrayBuffer(24);
var idView = new Uint32Array(buffer, 0, 2);
test({}, buffer);
test({0:0,1:0}, idView);

// with prototypes
{
  __v_0 = Object.create(null);
  __v_1 = Object.create(__v_0);
  test({}, __v_0);
  test({}, __v_1);
  function F0() {}
  const v2 = new F0();
  test({}, v2);
}

// null prototype
{
  function F0() {
      this.b = 4294967297;
  }
  const v3 = new F0();
  const o4 = {
      __proto__: v3,
  };
  const o7 = {
      ...o4,
      __proto__: null,
  };
  const o6 = {
      ...v3,
      __proto__: null,
  };
  assertEquals(o6, {b: 4294967297});
  assertEquals(o7, {});
  test({}, o7);
  test({b: 4294967297}, o6);
}

// Dictionary mode objects
{
  let v0 = "";
  for (let v1 = 0; v1 < 250; v1++) {
      v0 += `this.a${v1} = 0;\n`;
  }
  const v4 = Function(v0);
  const v5 = new v4();
  const o6 = {
     ...v5,
  };
  test(o6, v5);
}

// Null proto with non data props
{
  const v0 = [1];
  const o3 = {
      ...v0,
      get g() {
      },
  };
  const o4 = {
      ...o3,
      __proto__: null,
  };
  test({0: 1, g: undefined}, o4);
}

// Ensure the IC without feedback vector supports the fast-case
(function() {
  var o = {};
  o.x = "1";
  var o2 = {...o};
  assertTrue(%HaveSameMap(o, o2));
})()
