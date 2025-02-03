// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-always-turbofan --no-stress-flush-code
// Flags: --expose-gc
// Files: test/mjsunit/code-coverage-utils.js

(async function () {

  %DebugToggleBlockCoverage(true);

  await TestCoverage(
    "class with no fields",
    `
class X {                                  // 000
};                                         // 050
    `,
    [ {"start":0,"end":99,"count":1} ]
  );

  await TestCoverage(
    "class that's not created",
    `
class X {                                  // 000
  x = function() { }                       // 050
};                                         // 100
    `,
    [ {"start":0,"end":149,"count":1},
      {"start":52,"end":70,"count":0} ]
  );

  await TestCoverage(
    "class with field thats not called",
    `
class X {                                  // 000
  x = function() { }                       // 050
};                                         // 100
let x = new X();                           // 150
    `,
    [ {"start":0,"end":199,"count":1},
      {"start":52,"end":70,"count":1},
      {"start":56,"end":70,"count":0} ]
  );

  await TestCoverage(
    "class field",
    `
class X {                                  // 000
  x = function() { }                       // 050
};                                         // 100
let x = new X();                           // 150
x.x();                                     // 200
    `,
    [ {"start":0,"end":249,"count":1},
      {"start":52,"end":70,"count":1},
      {"start":56,"end":70,"count":1} ]
  );

  await TestCoverage(
    "non contiguous class field",
    `
class X {                                  // 000
  x = function() { }                       // 050
  foo() { }                                // 100
  y = function() {}                        // 150
};                                         // 200
let x = new X();                           // 250
x.x();                                     // 300
x.y();                                     // 350
    `,
    [ {"start":0,"end":399,"count":1},
      {"start":52,"end":169,"count":1},
      {"start":56,"end":70,"count":1},
      {"start":102,"end":111,"count":0},
      {"start":156,"end":169,"count":1} ]
  );

  await TestCoverage(
    "non contiguous class field thats called",
    `
class X {                                  // 000
  x = function() { }                       // 050
  foo() { }                                // 100
  y = function() {}                        // 150
};                                         // 200
let x = new X();                           // 250
x.x();                                     // 300
x.y();                                     // 350
x.foo();                                   // 400
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":52,"end":169,"count":1},
      {"start":56,"end":70,"count":1},
      {"start":102,"end":111,"count":1},
      {"start":156,"end":169,"count":1} ]
  );

  await TestCoverage(
    "class with initializer iife",
    `
class X {                                  // 000
  x = (function() { })()                   // 050
};                                         // 100
let x = new X();                           // 150
    `,
    [ {"start":0,"end":199,"count":1},
      {"start":52,"end":74,"count":1},
      {"start":57,"end":71,"count":1} ]
  );

  await TestCoverage(
    "class with computed field",
    `
function f() {};                           // 000
class X {                                  // 050
  [f()] = (function() { })()               // 100
};                                         // 150
let x = new X();                           // 200
    `,
    [ {"start":0,"end":249,"count":1},
      {"start":0,"end":15,"count":1},
      {"start":102,"end":128,"count":1},
      {"start":111,"end":125,"count":1} ]
  );

  await TestCoverage(
    "static class field that's not called",
    `
class X {                                  // 000
  static x = function() { }                // 050
};                                         // 100
    `,
    [ {"start":0,"end":149,"count":1},
      {"start":52,"end":77,"count":1},
      {"start":63,"end":77,"count":0} ]
  );

  await TestCoverage(
    "static class field",
    `
class X {                                  // 000
  static x = function() { }                // 050
};                                         // 100
X.x();                                     // 150
    `,
    [ {"start":0,"end":199,"count":1},
      {"start":52,"end":77,"count":1},
      {"start":63,"end":77,"count":1} ]
  );

  await TestCoverage(
    "static class field with iife",
    `
class X {                                  // 000
  static x = (function() { })()            // 050
};                                         // 100
    `,
    [ {"start":0,"end":149,"count":1},
      {"start":52,"end":81,"count":1},
      {"start":64,"end":78,"count":1} ]
  );

  await TestCoverage(
    "computed static class field",
    `
function f() {}                            // 000
class X {                                  // 050
  static [f()] = (function() { })()        // 100
};                                         // 150
    `,
    [ {"start":0,"end":199,"count":1},
      {"start":0,"end":15,"count":1},
      {"start":102,"end":135,"count":1},
      {"start":118,"end":132,"count":1} ]
  );

})();
