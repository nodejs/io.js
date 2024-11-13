// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --allow-natives-syntax

// Clone IC trying to cache a map in a full transition array.
(function () {
  // Let's overflow some transition arrays.
  for (var i = 0; i < 2000; i++) {
    var key = (Math.random() + 1).toString(36).substring(2);
    var v = {};
    eval("v.p"+key+"=1;");
  }
  function cloneic() {
    v = {};
    const o10 = {
        ...v,
    };
  }
  %PrepareFunctionForOptimization(cloneic);
  cloneic();
})();
