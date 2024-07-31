// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// Test polymorphic Array.prototype.push with multiple maps per ElementsKind.

function push(arr) {
  return arr.push(99);
}

let double_arr = [2.1, 13.37];
let holey_double_arr = [, 4.9];
let smi_arr1 = [12, 42];
let smi_arr2 = [47, 11];
double_arr.a = "";
holey_double_arr.b = "";
smi_arr1.c = "";
smi_arr2.d = "";

%PrepareFunctionForOptimization(push);
assertEquals(3, push(double_arr));
assertEquals(3, push(holey_double_arr));
assertEquals(3, push(smi_arr1));
assertEquals(3, push(smi_arr2));
%OptimizeFunctionOnNextCall(push);
assertEquals(4, push(double_arr));
assertEquals(4, push(holey_double_arr));
assertEquals(4, push(smi_arr1));
assertEquals(4, push(smi_arr2));

assertEquals([2.1, 13.37, 99, 99], double_arr);
assertEquals([, 4.9, 99, 99], holey_double_arr);
assertEquals([12, 42, 99, 99], smi_arr1);
assertEquals([47, 11, 99, 99], smi_arr2);
