// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --reuse-scope-infos --expose-gc --stress-flush-code

  function executeCode(code) {
    if (typeof code === 'function') return code();
  }
  assertThrows = function assertThrows(code) {
      executeCode(code);
  };
function __getRandomProperty() {
}
(function () {
  __callGC = function () {
      gc();
  };
})();
  var __v_25 = [];
function __f_10(__v_29, __v_30) {
    var __v_31 = Realm.create();
  try {
    assertThrows(function () {
        Realm.eval(__v_31, __v_29[
        __v_29.length - 1]);
    }, Realm. __v_30);
  } catch (e) {}
    delete __v_25[__getRandomProperty()], __callGC();
}
  var __v_28 = [{
    scripts: ['eval("function NaN() {}");'],
  }, {
    scripts: [`
      `.replace()],
  }];
  __v_28.forEach(function (__v_33) {
      __f_10(__v_33.scripts, __v_33.expectedError);
      __f_10(__v_33.scripts, __v_33.expectedError);
  });
