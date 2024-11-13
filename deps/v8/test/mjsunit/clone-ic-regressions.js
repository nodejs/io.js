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
})();

// Cloning sealed and frozen objects
(function(){
  function testFrozen1(x) {
    "use strict"
    if (x.x) {
      assertThrows(function() { x.x = 42 }, TypeError);
    }
    if (x[2]) {
      assertThrows(function() { x[2] = 42 }, TypeError);
    }
  }

  function testFrozen2(x) {
    if (x.x) {
      x.x = 42;
      assertFalse(x.x == 42);
    }
    if (x[2]) {
      x[10] = 42;
      assertFalse(x[10] == 42);
    }
  }

  function testUnfrozen(x) {
    x.x = 42;
    assertTrue(x.x == 42);
    x[2] = 42;
    assertTrue(x[2] == 42);
  }

  function testSealed(x) {
    x.asdf = 42;
    assertFalse(x.asdf == 42);
    x[10] = 42;
    assertFalse(x[10] == 42);
  }

  function testUnsealed(x) {
    x.asdf = 42;
    assertTrue(x.asdf == 42);
    x[10] = 42;
    assertTrue(x[10] == 42);
  }

  function testFreeze(x) {
    Object.freeze(x);
    testFrozen1(x);
    testFrozen2(x);
    var y = {...x};
    assertFalse(%HaveSameMap(x,y));
    if (x.__proto__ == Object.prototype) {
      assertEquals(x, y);
    }
    testUnfrozen(y);
    y = Object.assign({}, x);
    if (x.__proto__ == Object.prototype) {
      assertEquals(x, y);
    }
    testUnfrozen(y);
  }

  function testSeal(x) {
    Object.seal(x);
    testSealed(x);
    var y = {...x};
    assertFalse(%HaveSameMap(x,y));
    if (x.__proto__ == Object.prototype) {
      assertEquals(x, y);
    }
    testUnsealed(y);
    y = Object.assign({}, x);
    if (x.__proto__ == Object.prototype) {
      assertEquals(x, y);
    }
    testUnsealed(y);
  }

  function testNonExtend(x) {
    Object.preventExtensions(x);
    testSealed(x);
    var y = {...x};
    assertFalse(%HaveSameMap(x,y));
    if (x.__proto__ == Object.prototype) {
      assertEquals(x, y);
    }
    testUnsealed(y);
    y = Object.assign({}, x);
    if (x.__proto__ == Object.prototype) {
      assertEquals(x, y);
    }
    testUnsealed(y);
  }


  var tests = [testFreeze, testSeal, testNonExtend];

  for (var i = 0; i < 20; ++i) {
    tests.forEach(test => {
      if (i < 10) { %ClearFunctionFeedback(test); }
      var x = {};
      x.x = 3;
      test(x);

      if (i < 10) { %ClearFunctionFeedback(test); }
      x = [];
      x[2]= 3;
      test(x);

      if (i < 10) { %ClearFunctionFeedback(test); }
      x = {};
      x[2]= 3;
      test(x);

      if (i < 10) { %ClearFunctionFeedback(test); }
      var x = {};
      x.x = 3;
      x[10000] = 3
      test(x);
    });
  }
})();

// A case not supported by the clone IC.
(function () {
  function F0() {}
  const v6 = new F0();
  o9 = {
   ...v6,
  };
})();

// A case where the clone IC adds a transition to an existing transition array.
(function () {
  function f1() {}
  function F2(a4) {
    function f5() {}
    a4.toString = f5;
  }
  const v7 = new F2(WeakSet);
  const v8 = new F2(f1);
  new F2(v8);
  const o10 = {
      ...v7,
  };
})();

// A case where we copy from a smaller into a bigger object.
(function() {
  function F0() {
  }
  function F3() {
      const v9 = new F0();
      const o10 = {
          ...v9,
      };
  }
  new F3();
  new F3();
  new F3();
})();
