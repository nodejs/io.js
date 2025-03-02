// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev --no-always-turbofan

function poly_store(a, x) {
  a[0] = x;
};

var y = new Array();
y[0] = 1.1;
y.x = 12;
// We don't use {y}, but it prevents the map packed double map with `x` from
// being GCed, which avoids {poly_store} from being deopted.

%PrepareFunctionForOptimization(poly_store);
poly_store([1.1], 'a');
poly_store([1.1], 2.1);
var x = new Array();
x[0] = 1.1;
x.x = 12;
poly_store(x, 'a');
%OptimizeMaglevOnNextCall(poly_store);

var c = new Array();
c[0] = 1.1;
assertTrue(%HasDoubleElements(c));
poly_store(c, 'a');
assertTrue(%HasObjectElements(c));
assertEquals(c[0], 'a');
assertTrue(isMaglevved(poly_store));
