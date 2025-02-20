// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

function store_f64_to_smi_field(v) {
  return {some_unique_name_U5d8Xe: Math.floor((v + 0xffffffff) - 0xfffffffe)}
          .some_unique_name_U5d8Xe;
}

%PrepareFunctionForOptimization(store_f64_to_smi_field);
assertEquals(1, store_f64_to_smi_field(0));
assertEquals(2, store_f64_to_smi_field(1));
%OptimizeFunctionOnNextCall(store_f64_to_smi_field);
assertEquals(2, store_f64_to_smi_field(1));
assertOptimized(store_f64_to_smi_field);

// Won't fit in Smi anymore, which should trigger a deopt
assertEquals(0xffffffff+0xffffffff-0xfffffffe,
            store_f64_to_smi_field(0xffffffff));
assertUnoptimized(store_f64_to_smi_field);
