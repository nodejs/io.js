// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

var number_of_appends = 0;
function append_to_array(element, index, array) {
  array.push(42);
  number_of_appends++;
}
// Prevent inlining.
%NeverOptimizeFunction(append_to_array);

var iteration_count_after_append = 0;
function count_iterations(element, index, array) {
  iteration_count_after_append++;
}
// Prevent inlining.
%NeverOptimizeFunction(count_iterations);


function append_to_array_in_forEach_then_iterate_again(arr) {
  // If a forEach appends to its array, it should only iterate over the original
  // length...
  arr.forEach(append_to_array);

  // ... but a subsequent forEach should use the new length of the array, not
  // the old one.
  arr.forEach(count_iterations);
}

%PrepareFunctionForOptimization(append_to_array_in_forEach_then_iterate_again);
number_of_appends = 0;
iteration_count_after_append = 0;
append_to_array_in_forEach_then_iterate_again([12, 34]);
assertEquals(number_of_appends, 2);
assertEquals(iteration_count_after_append, 4);

%OptimizeFunctionOnNextCall(append_to_array_in_forEach_then_iterate_again);
number_of_appends = 0;
iteration_count_after_append = 0;
append_to_array_in_forEach_then_iterate_again([12, 34]);
assertEquals(number_of_appends, 2);
assertEquals(iteration_count_after_append, 4);
