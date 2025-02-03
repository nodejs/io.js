// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --allow-natives-syntax

(function() {
  let obj = {};
  // Add a property
  Object.defineProperty(obj, "__proto__", { enumerable: true, writable: true });
  // Adds side-step from {__proto__} -> {}
  Object.assign({}, obj);
  // Generalizes {__proto__}
  Object.defineProperty(obj, "__proto__", { value: 0 });
})();

(function() {
  var a = {};
  a.asdf = 42;
  Object.defineProperty(Object.prototype, "asdf", {get(){assertTrue(false);},
                                                   set(v){Object.defineProperty(this, "asdf", {value:100})}});
  assertTrue(Object.assign({}, a).asdf === 100);
  assertTrue(Object.assign({}, a).asdf === 100);
  assertTrue(Object.assign({}, a).asdf === 100);
})();

(function() {
  var a = {};
  a.x = 42;
  var b = {};
  b.y = 42;
  Object.assign({}, a);
  Object.defineProperty(Object.prototype, "x", {set(v) {}, configurable: true});
  assertTrue(Object.assign({}, a).x === undefined);
  assertTrue(Object.assign({}, b).y === 42);
  assertTrue(Object.assign({}, a).x === undefined);
  assertTrue(Object.assign({}, b).y === 42);
  Object.defineProperty(Object.prototype, "y", {set(v) {}, configurable: true});
  assertTrue(Object.assign({}, a).x === undefined);
  assertTrue(Object.assign({}, b).y === undefined);
  assertTrue(Object.assign({}, a).x === undefined);
  assertTrue(Object.assign({}, b).y === undefined);
})();

(function() {
  let a = {};
  a.qwer1 = 42;
  Object.defineProperty(Object.prototype, "qwer1", { writable: false });
  assertThrows(function(){ Object.assign({}, a) });
  assertThrows(function(){ Object.assign({}, a) });
  a = {};
  a.qwer2 = 42;
  Object.assign({}, a);
  Object.assign({}, a);
  Object.defineProperty(Object.prototype, "qwer2", { configurable: false });
  assertThrows(function(){ Object.assign({}, a) });
  assertThrows(function(){ Object.assign({}, a) });
})();
