// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-gc

// Check proto transitions are applied first
(function() {
  var p = {};

  var x1 = {};
  x1.a = 1;
  x1.__proto__ = p;

  var x2 = {};
  x2.__proto__ = p;
  x2.a = 1;

  assertTrue(%HaveSameMap(x1, x2));
})();

// Check proto transitions can be undone
(function() {
  var x1 = {};
  x1.a = 1;

  var x2 = {};
  x2.a = 1;
  x2.__proto__ = {};
  x2.__proto__ = x1.__proto__;

  assertTrue(%HaveSameMap(x1, x2));
})();


// Checks that intermediate maps don't die after proto transitions
(function() {
  function foo() {}
  function goo() {}
  var p = new goo();

  function test() {
    var o = new foo();
    o.middle = 2;
    o.__proto__ = p;
    return o;
  }

  var a = test();
  gc();gc();gc();
  var b = test();
  assertTrue(%HaveSameMap(a,b));
})();

// Checks if slack tracking on derived maps updates actual root map
(function() {
  function bla(a,b){}
  pro = new Proxy({},{});
  var co = function(){}
  co.prototype = new Proxy({}, {get(target, property) {return 1}});

  Reflect.construct(bla, [], co);
  Reflect.construct(bla, [], co);
  Reflect.construct(bla, [], co);
  Reflect.construct(bla, [], co);
  Reflect.construct(bla, [], co);
  Reflect.construct(bla, [], co);
  Reflect.construct(bla, [], co);

  var x = new bla();
  x.__proto__ = co.prototype;
})();
