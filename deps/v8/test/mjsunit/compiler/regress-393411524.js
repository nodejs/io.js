// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-use-ic --stress-concurrent-inlining

function __wrapTC(f) {
  try {
    return f();
  } catch (e) {}
}

(function () {
  var __v_0 = __wrapTC(() => function () {
    var __v_3 = __wrapTC(() =>
        [ this]);
    var __v_4 = "";
    for (var __v_5 in __v_3) {
      __v_4 +=
          __v_5[__v_5];
    }
    return __v_4;
  });
  for (var __v_1 = 0; __v_1 < 1000; ++__v_1) {
    var __v_2 = __wrapTC(() => __v_0());
    try {
      if (__v_2 !== "1235") throw new Error("bad result got: " + __v_2);
    } catch (e) {}
  }
})();
