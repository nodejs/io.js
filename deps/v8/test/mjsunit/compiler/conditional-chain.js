// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generates a function of the form
//
// function(x) {
//   return (x === threshold) ? threshold*threshold :
//          (x === threshold+1) ? (threshold+1)*(threshold+1) :
//          ...
//          (x === threshold+num_if_else-1) ?
//              (threshold+num_if_else-1)*(threshold+num_if_else-1) :
//          (threshold+num_if_else)*(threshold+num_if_else);
// }
//
function generate_long_conditional_chain(num_if_else, threshold = 0) {
    let str = "(function(x){";
    str += "return ";
    for (let j = 0; j < num_if_else; j++) {
        let i = j + threshold;
        str += "(x === "+i+") ? "+i*i+" : ";
    }
    let i = num_if_else + threshold;
    str += i*i+";})";
    return eval(str);
}

// Generates a function of the form
//
// function(x, y) {
//   return (x === 0 && y === 0) ? 0*0 + threshold :
//          (x === 0 && y === 1) ? 0*1 + threshold :
//          ...
//          (x === 1 && y === 1) ? 1*1 + threshold :
//          (x === 1 && y === 2) ? 1*2 + threshold :
//          ...
//          (x === 2 && y === 1) ? 2*1 + threshold :
//          (x === 2 && y === 2) ? 2*2 + threshold :
//          (x === 2 && y === 3) ? 2*3 + threshold :
//          ...
//          (x === num_x-1 && y === num_y-1) ?
//              (num_x-1)*(num_y-1) + threshold :
//          (num_x)*(num_y) + threshold;
function generate_double_conditional_chain(num_x, num_y, threshold = 0) {
    let str = "(function(x, y){";
    str += "return ";
    for (let i = 0; i < num_x; i++) {
        for (let j = 0; j < num_y; j++) {
            str += "(x === "+i+" && y === "+j+") ? "+(i*j + threshold)+" : ";
        }
    }
    str += (num_x*num_y + threshold)+";})";
    return eval(str);
}

(function() {
    let conditional_chain = generate_long_conditional_chain(110);
    assertEquals(5*5, conditional_chain(5));
    assertEquals(6*6, conditional_chain(6));
    assertEquals(100*100, conditional_chain(100));
    assertEquals(109*109, conditional_chain(109));
    assertEquals(110*110, conditional_chain(110));
    assertEquals(110*110, conditional_chain(111));
    assertEquals(110*110, conditional_chain(200));
    assertEquals(110*110, conditional_chain(1000));
})();

// Test that the result of the conditional chain is correct for a double
// conditional chain. While the result given should be correct, there should
// not be any crashes even if the length of the chain is very long.
(function() {
    let threshold = 100;
    let double_conditional_chain =
        generate_double_conditional_chain(17, 19, threshold);
    assertEquals(5*5 + threshold, double_conditional_chain(5, 5));
    assertEquals(6*6 + threshold, double_conditional_chain(6, 6));
    assertEquals(4*5 + threshold, double_conditional_chain(4, 5));
    assertEquals(5*4 + threshold, double_conditional_chain(5, 4));
    assertEquals(9*2 + threshold, double_conditional_chain(9, 2));
    assertEquals(17*19 + threshold, double_conditional_chain(100, 12));
    assertEquals(17*19 + threshold, double_conditional_chain(100, 100));
    assertEquals(17*19 + threshold, double_conditional_chain(100, 1000));
    assertEquals(17*19 + threshold, double_conditional_chain(1000, 100));
    assertEquals(17*19 + threshold, double_conditional_chain(1000, 1000));
})();

// Test that the result of the conditional chain is correct.
// The size of the chain is stretched to 50000 to make sure that the
// conditional chain will not crash even if the length of the chain is very
// long.
(function() {
    let threshold = 100;
    let chain_length = 50000;
    let conditional_chain =
        generate_long_conditional_chain(chain_length, threshold);
    assertEquals(
        (chain_length + threshold)*(chain_length + threshold),
        conditional_chain(0));
    assertEquals(
        (chain_length + threshold)*(chain_length + threshold),
        conditional_chain(1));
    assertEquals(
        (1 + threshold)*(1 + threshold),
        conditional_chain(1 + threshold));
    assertEquals(
        (2 + threshold)*(2 + threshold),
        conditional_chain(2 + threshold));
    assertEquals(
        (chain_length - 1 + threshold)*(chain_length - 1 + threshold),
        conditional_chain(chain_length - 1 + threshold));
    assertEquals(
        (chain_length + threshold)*(chain_length + threshold),
        conditional_chain(chain_length + threshold));
    assertEquals(
        (chain_length + threshold)*(chain_length + threshold),
        conditional_chain(chain_length + 1 + threshold));
    assertEquals(
        (chain_length + threshold)*(chain_length + threshold),
        conditional_chain(chain_length + 100 + threshold));
    assertEquals(
        (chain_length + threshold)*(chain_length + threshold),
        conditional_chain(chain_length + 1000 + threshold));
})();
