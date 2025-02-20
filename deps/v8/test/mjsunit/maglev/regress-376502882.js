// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function main() {

  function func(arr) {
    for (let i = 0; i < 256; i++) {
      arr[i] = x;
      var x = i;
      for (let j = 0; j < 8; j++) {
          x >>= 1;
      }
      arr[i] = x;
    }
  }

  %PrepareFunctionForOptimization(func);
  var arr = [];
  for (let i = 0; i < 256; ++i) { arr[i] = i; }
  func(arr);
}

main();
main();
