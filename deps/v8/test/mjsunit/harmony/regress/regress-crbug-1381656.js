// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// Returning arguments for a function with 1 parameter results in toSorted code
// initially under-allocating a sorting worklist of length 1 (instead of
// 2). This then results the worklist growing to length 17, with elements 2-16
// being holes. The hole values should not be accessed.
let args = (function(x) {
  return arguments;
})(1, 2);
Array.prototype.toSorted.call(args);
