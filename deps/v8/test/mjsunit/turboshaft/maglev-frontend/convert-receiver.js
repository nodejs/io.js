// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

var object = { f: function() { return 42; }, x: 42 };
delete object.x;

function call_f_receiver_obj(o) {
  return o.f();
}

%PrepareFunctionForOptimization(call_f_receiver_obj);
assertEquals(42, call_f_receiver_obj(object));
%OptimizeFunctionOnNextCall(call_f_receiver_obj);
assertEquals(42, call_f_receiver_obj(object));
assertOptimized(call_f_receiver_obj);


function call_f_receiver_number(o) {
  return o.f();
}

Number.prototype.f = function() { return 12 };
%PrepareFunctionForOptimization(call_f_receiver_number);
assertEquals(12, call_f_receiver_number(7));
%OptimizeFunctionOnNextCall(call_f_receiver_number);
assertEquals(12, call_f_receiver_number(7));
assertEquals(12, call_f_receiver_number(4.56));
assertOptimized(call_f_receiver_number);


function f() {
  return this.x;
}

function call_f_receiver_undefined(o) {
  return f.call(o);
}

%PrepareFunctionForOptimization(call_f_receiver_undefined);
assertEquals(undefined, call_f_receiver_undefined(undefined));
assertEquals(undefined, call_f_receiver_undefined(null));
%OptimizeFunctionOnNextCall(call_f_receiver_undefined);
assertEquals(undefined, call_f_receiver_undefined(undefined));
assertEquals(undefined, call_f_receiver_undefined(null));
assertOptimized(call_f_receiver_undefined);
// Since the global proxy is passed to `f`, changing `x` here should change the
// return value of `f`.
var x = 23;
assertEquals(x, call_f_receiver_undefined(undefined));
assertEquals(x, call_f_receiver_undefined(null));
assertOptimized(call_f_receiver_undefined);
