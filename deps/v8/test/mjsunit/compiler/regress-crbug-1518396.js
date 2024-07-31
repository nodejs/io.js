// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function get_thin_string(a, b) {
  var str = a + b;
  var o = {};
  o[str];
  return str;
}

var str = get_thin_string("bar");

function CheckCS() {
  return str.charCodeAt();
}

%PrepareFunctionForOptimization(CheckCS);
assertEquals(CheckCS(), 98);
%OptimizeFunctionOnNextCall(CheckCS);
assertEquals(CheckCS(), 98);
