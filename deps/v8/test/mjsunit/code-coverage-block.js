// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-always-turbofan --no-stress-flush-code
// Flags: --expose-gc --js-staging
// Files: test/mjsunit/code-coverage-utils.js

(async function () {

  %DebugToggleBlockCoverage(true);

  await TestCoverage(
    "call an IIFE",
    `
(function f() {})();
    `,
    [ {"start":0,"end":20,"count":1},
      {"start":1,"end":16,"count":1} ]
  );

  await TestCoverage(
    "call locally allocated function",
    `
let f = () => 1; f();
    `,
    [ {"start":0,"end":21,"count":1},
      {"start":8,"end":15,"count":1} ]
  );

  await TestCoverage(
    "if statements",
    `
function g() {}                           // 0000
function f(x) {                           // 0050
  if (x == 42) {                          // 0100
    if (x == 43) g(); else g();           // 0150
  }                                       // 0200
  if (x == 42) { g(); } else { g(); }     // 0250
  if (x == 42) g(); else g();             // 0300
  if (false) g(); else g();               // 0350
  if (false) g();                         // 0400
  if (true) g(); else g();                // 0450
  if (true) g();                          // 0500
}                                         // 0550
f(42);                                    // 0600
f(43);                                    // 0650
if (true) {                               // 0700
  const foo = 'bar';                      // 0750
} else {                                  // 0800
  const bar = 'foo';                      // 0850
}                                         // 0900
    `,
    [ {"start":0,"end":949,"count":1},
      {"start":801,"end":901,"count":0},
      {"start":0,"end":15,"count":11},
      {"start":50,"end":551,"count":2},
      {"start":115,"end":203,"count":1},
      {"start":167,"end":171,"count":0},
      {"start":265,"end":287,"count":1},
      {"start":315,"end":329,"count":1},
      {"start":363,"end":367,"count":0},
      {"start":413,"end":417,"count":0},
      {"start":466,"end":476,"count":0} ]
  );

  await TestCoverage(
    "if statement (early return)",
    `
!function() {                             // 0000
  if (true) {                             // 0050
    nop();                                // 0100
    return;                               // 0150
    nop();                                // 0200
  }                                       // 0250
  nop();                                  // 0300
}()                                       // 0350
    `,
    [ {"start":0,"end":399,"count":1},
      {"start":1,"end":351,"count":1},
      {"start":161,"end":350,"count":0} ]
  );

  await TestCoverage(
    "if statement (no semi-colon)",
    `
!function() {                             // 0000
  if (true) nop()                         // 0050
  if (true) nop(); else nop()             // 0100
  nop();                                  // 0150
}()                                       // 0200
    `,
    [ {"start":0,"end":249,"count":1},
      {"start":1,"end":201,"count":1},
      {"start":118,"end":129,"count":0} ]
  );

  await TestCoverage(
    "for statements",
    `
function g() {}                           // 0000
!function() {                             // 0050
  for (var i = 0; i < 12; i++) g();       // 0100
  for (var i = 0; i < 12; i++) {          // 0150
    g();                                  // 0200
  }                                       // 0250
  for (var i = 0; false; i++) g();        // 0300
  for (var i = 0; true; i++) break;       // 0350
  for (var i = 0; i < 12; i++) {          // 0400
    if (i % 3 == 0) g(); else g();        // 0450
  }                                       // 0500
}();                                      // 0550
    `,
    [ {"start":0,"end":599,"count":1},
      {"start":0,"end":15,"count":36},
      {"start":51,"end":551,"count":1},
      {"start":131,"end":135,"count":12},
      {"start":181,"end":253,"count":12},
      {"start":330,"end":334,"count":0},
      {"start":431,"end":503,"count":12},
      {"start":470,"end":474,"count":4},
      {"start":474,"end":484,"count":8} ]
  );

  await TestCoverage(
    "for statements pt. 2",
    `
function g() {}                           // 0000
!function() {                             // 0050
  let j = 0;                              // 0100
  for (let i = 0; i < 12; i++) g();       // 0150
  for (const i = 0; j < 12; j++) g();     // 0200
  for (j = 0; j < 12; j++) g();           // 0250
  for (;;) break;                         // 0300
}();                                      // 0350
    `,
    [ {"start":0,"end":399,"count":1},
      {"start":0,"end":15,"count":36},
      {"start":51,"end":351,"count":1},
      {"start":181,"end":185,"count":12},
      {"start":233,"end":237,"count":12},
      {"start":277,"end":281,"count":12} ]
  );

  await TestCoverage(
    "for statements (no semicolon)",
    `
function g() {}                           // 0000
!function() {                             // 0050
  for (let i = 0; i < 12; i++) g()        // 0100
  for (let i = 0; i < 12; i++) break      // 0150
  for (let i = 0; i < 12; i++) break; g() // 0200
}();                                      // 0250
    `,
    [ {"start":0,"end":299,"count":1},
      {"start":0,"end":15,"count":13},
      {"start":51,"end":251,"count":1},
      {"start":131,"end":134,"count":12} ]
  );

  await TestCoverage(
    "for statement (early return)",
    `
!function() {                             // 0000
  for (var i = 0; i < 10; i++) {          // 0050
    nop();                                // 0100
    continue;                             // 0150
    nop();                                // 0200
  }                                       // 0250
  nop();                                  // 0300
  for (;;) {                              // 0350
    nop();                                // 0400
    break;                                // 0450
    nop();                                // 0500
  }                                       // 0550
  nop();                                  // 0600
  for (;;) {                              // 0650
    nop();                                // 0700
    return;                               // 0750
    nop();                                // 0800
  }                                       // 0850
  nop();                                  // 0900
}()                                       // 0950
    `,
    [ {"start":0,"end":999,"count":1},
      {"start":1,"end":951,"count":1},
      {"start":81,"end":253,"count":10},
      {"start":163,"end":253,"count":0},
      {"start":460,"end":553,"count":0},
      {"start":761,"end":950,"count":0} ]
  );

  await TestCoverage(
    "for-of and for-in statements",
    `
!function() {                             // 0000
  var i;                                  // 0050
  for (i of [0,1,2,3]) { nop(); }         // 0100
  for (let j of [0,1,2,3]) { nop(); }     // 0150
  for (i in [0,1,2,3]) { nop(); }         // 0200
  for (let j in [0,1,2,3]) { nop(); }     // 0250
  var xs = [{a:0, b:1}, {a:1,b:0}];       // 0300
  for (var {a: x, b: y} of xs) { nop(); } // 0350
}();                                      // 0400
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":1,"end":401,"count":1},
      {"start":123,"end":133,"count":4},
      {"start":177,"end":187,"count":4},
      {"start":223,"end":233,"count":4},
      {"start":277,"end":287,"count":4},
      {"start":381,"end":391,"count":2} ]
  );

  await TestCoverage(
    "while and do-while statements",
    `
function g() {}                           // 0000
!function() {                             // 0050
  var i;                                  // 0100
  i = 0; while (i < 12) i++;              // 0150
  i = 0; while (i < 12) { g(); i++; }     // 0200
  i = 0; while (false) g();               // 0250
  i = 0; while (true) break;              // 0300
                                          // 0350
  i = 0; do i++; while (i < 12);          // 0400
  i = 0; do { g(); i++; }                 // 0450
         while (i < 12);                  // 0500
  i = 0; do { g(); } while (false);       // 0550
  i = 0; do { break; } while (true);      // 0600
}();                                      // 0650
    `,
    [ {"start":0,"end":699,"count":1},
      {"start":0,"end":15,"count":25},
      {"start":51,"end":651,"count":1},
      {"start":174,"end":178,"count":12},
      {"start":224,"end":237,"count":12},
      {"start":273,"end":277,"count":0},
      {"start":412,"end":416,"count":12},
      {"start":462,"end":475,"count":12} ]
  );

  await TestCoverage(
    "while statement (early return)",
    `
!function() {                             // 0000
  let i = 0;                              // 0050
  while (i < 10) {                        // 0100
    i++;                                  // 0150
    continue;                             // 0200
    nop();                                // 0250
  }                                       // 0300
  nop();                                  // 0350
  while (true) {                          // 0400
    nop();                                // 0450
    break;                                // 0500
    nop();                                // 0550
  }                                       // 0600
  nop();                                  // 0650
  while (true) {                          // 0700
    nop();                                // 0750
    return;                               // 0800
    nop();                                // 0850
  }                                       // 0900
  nop();                                  // 0950
}()                                       // 1000
    `,
    [ {"start":0,"end":1049,"count":1},
      {"start":1,"end":1001,"count":1},
      {"start":117,"end":303,"count":10},
      {"start":213,"end":303,"count":0},
      {"start":510,"end":603,"count":0},
      {"start":811,"end":1000,"count":0} ]
  );

  await TestCoverage(
    "do-while statement (early return)",
    `
!function() {                             // 0000
  let i = 0;                              // 0050
  do {                                    // 0100
    i++;                                  // 0150
    continue;                             // 0200
    nop();                                // 0250
  } while (i < 10);                       // 0300
  nop();                                  // 0350
  do {                                    // 0400
    nop();                                // 0450
    break;                                // 0500
    nop();                                // 0550
  } while (true);                         // 0600
  nop();                                  // 0650
  do {                                    // 0700
    nop();                                // 0750
    return;                               // 0800
    nop();                                // 0850
  } while (true);                         // 0900
  nop();                                  // 0950
}()                                       // 1000
    `,
    [ {"start":0,"end":1049,"count":1},
      {"start":1,"end":1001,"count":1},
      {"start":105,"end":303,"count":10},
      {"start":213,"end":303,"count":0},
      {"start":510,"end":603,"count":0},
      {"start":811,"end":1000,"count":0} ]
  );

  await TestCoverage(
    "return statements",
    `
!function() { nop(); return; nop(); }();  // 0000
!function() { nop(); return 42;           // 0050
              nop(); }();                 // 0100
    `,
    [ {"start":0,"end":149,"count":1},
      {"start":1,"end":37,"count":1},
      {"start":28,"end":36,"count":0},
      {"start":51,"end":122,"count":1},
      {"start":81,"end":121,"count":0} ]
  );

  await TestCoverage(
    "try/catch/finally statements",
    `
!function() {                             // 0000
  try { nop(); } catch (e) { nop(); }     // 0050
  try { nop(); } finally { nop(); }       // 0100
  try {                                   // 0150
    try { throw 42; } catch (e) { nop(); }// 0200
  } catch (e) { nop(); }                  // 0250
  try {                                   // 0300
    try { throw 42; } finally { nop(); }  // 0350
  } catch (e) { nop(); }                  // 0400
  try {                                   // 0450
    throw 42;                             // 0500
  } catch (e) {                           // 0550
    nop();                                // 0600
  } finally {                             // 0650
    nop();                                // 0700
  }                                       // 0750
}();                                      // 0800
    `,
    [ {"start":0,"end":849,"count":1},
      {"start":1,"end":801,"count":1},
      {"start":67,"end":87,"count":0},
      {"start":254,"end":274,"count":0} ]
  );

  await TestCoverage(
    "try/catch/finally statements with early return",
    `
!function() {                             // 0000
  try { throw 42; } catch (e) { return; } // 0050
  nop();                                  // 0100
}();                                      // 0150
!function() {                             // 0200
  try { throw 42; } catch (e) {}          // 0250
  finally { return; }                     // 0300
  nop();                                  // 0350
}();                                      // 0400
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":1,"end":151,"count":1},
      {"start":91,"end":150,"count":0},
      {"start":201,"end":401,"count":1},
      {"start":321,"end":400,"count":0} ]
  );

  await TestCoverage(
    "early return in blocks",
    `
!function() {                             // 0000
  try { throw 42; } catch (e) { return; } // 0050
  nop();                                  // 0100
}();                                      // 0150
!function() {                             // 0200
  try { nop(); } finally { return; }      // 0250
  nop();                                  // 0300
}();                                      // 0350
!function() {                             // 0400
  {                                       // 0450
    let x = 42;                           // 0500
    return () => x;                       // 0550
  }                                       // 0600
  nop();                                  // 0650
}();                                      // 0700
!function() {                             // 0750
  try { throw 42; } catch (e) {           // 0800
    return;                               // 0850
    nop();                                // 0900
  }                                       // 0950
  nop();                                  // 1000
}();                                      // 1050
    `,
    [ {"start":0,"end":1099,"count":1},
      {"start":1,"end":151,"count":1},
      {"start":91,"end":150,"count":0},
      {"start":201,"end":351,"count":1},
      {"start":286,"end":350,"count":0},
      {"start":401,"end":701,"count":1},
      {"start":603,"end":700,"count":0},
      {"start":561,"end":568,"count":0},
      {"start":751,"end":1051,"count":1},
      {"start":861,"end":1050,"count":0} ]
  );

  await TestCoverage(
    "switch statements",
    `
!function() {                             // 0000
  var x = 42;                             // 0050
  switch (x) {                            // 0100
    case 41: nop(); break;                // 0150
    case 42: nop(); break;                // 0200
    default: nop(); break;                // 0250
  }                                       // 0300
}();                                      // 0350
    `,
    [ {"start":0,"end":399,"count":1},
      {"start":1,"end":351,"count":1},
      {"start":154,"end":176,"count":0},
      {"start":254,"end":276,"count":0} ]
  );

  await TestCoverage(
    "labeled break statements",
    `
!function() {                             // 0000
  var x = 42;                             // 0050
  l0: switch (x) {                        // 0100
  case 41: return;                        // 0150
  case 42:                                // 0200
    switch (x) { case 42: break l0; }     // 0250
    break;                                // 0300
  }                                       // 0350
  l1: for (;;) {                          // 0400
    for (;;) break l1;                    // 0450
  }                                       // 0500
  l2: while (true) {                      // 0550
    while (true) break l2;                // 0600
  }                                       // 0650
  l3: do {                                // 0700
    do { break l3; } while (true);        // 0750
  } while (true);                         // 0800
  l4: { break l4; }                       // 0850
  l5: for (;;) for (;;) break l5;         // 0900
}();                                      // 0950
    `,
    [ {"start":0,"end":999,"count":1},
      {"start":1,"end":951,"count":1},
      {"start":152,"end":168,"count":0},
      {"start":287,"end":310,"count":0} ]
  );

  await TestCoverage(
    "labeled continue statements",
    `
!function() {                             // 0000
  l0: for (var i0 = 0; i0 < 2; i0++) {    // 0050
    for (;;) continue l0;                 // 0100
  }                                       // 0150
  var i1 = 0;                             // 0200
  l1: while (i1 < 2) {                    // 0250
    i1++;                                 // 0300
    while (true) continue l1;             // 0350
  }                                       // 0400
  var i2 = 0;                             // 0450
  l2: do {                                // 0500
    i2++;                                 // 0550
    do { continue l2; } while (true);     // 0600
  } while (i2 < 2);                       // 0650
}();                                      // 0700
    `,
    [ {"start":0,"end":749,"count":1},
      {"start":1,"end":701,"count":1},
      {"start":87,"end":153,"count":2},
      {"start":271,"end":403,"count":2},
      {"start":509,"end":653,"count":2} ]
  );

  await TestCoverage(
    "conditional expressions",
    `
var TRUE = true;                          // 0000
var FALSE = false;                        // 0050
!function() {                             // 0100
  TRUE ? nop() : nop();                   // 0150
  true ? nop() : nop();                   // 0200
  false ? nop() : nop();                  // 0250
  FALSE ? TRUE ? nop()                    // 0300
               : nop()                    // 0350
        : nop();                          // 0400
  TRUE ? FALSE ? nop()                    // 0450
               : nop()                    // 0500
       : nop();                           // 0550
  TRUE ? nop() : FALSE ? nop()            // 0600
                       : nop();           // 0650
  FALSE ? nop() : TRUE ? nop()            // 0700
                       : nop();           // 0750
}();                                      // 0800
    `,
    [ {"start":0,"end":849,"count":1},
      {"start":101,"end":801,"count":1},
      {"start":165,"end":172,"count":0},
      {"start":215,"end":222,"count":0},
      {"start":258,"end":265,"count":0},
      {"start":308,"end":372,"count":0},
      {"start":465,"end":472,"count":0},
      {"start":557,"end":564,"count":0},
      {"start":615,"end":680,"count":0},
      {"start":708,"end":715,"count":0},
      {"start":773,"end":780,"count":0} ]
  );

  await TestCoverage(
    "yield expressions",
    `
const it = function*() {                  // 0000
  yield nop();                            // 0050
  yield nop() ? nop() : nop()             // 0100
  return nop();                           // 0150
}();                                      // 0200
it.next(); it.next();                     // 0250
    `,
    [ {"start":0,"end":299,"count":1},
      {"start":11,"end":201,"count":1},
      {"start":114,"end":121,"count":0},
      {"start":129,"end":200,"count":0} ]
  );

  await TestCoverage(
    "yield expressions twice",
    `
function* gen() {                         // 0000
  yield nop();                            // 0050
  yield nop() ? nop() : nop()             // 0100
  return nop();                           // 0150
};                                        // 0200
{const it = gen(); it.next(); it.next();} // 0250
{const it = gen(); it.next(); it.next();} // 0300
    `,
    [ {"start":0,"end":349,"count":1},
      {"start":0,"end":201,"count":2},
      {"start":114,"end":121,"count":0},
      {"start":129,"end":200,"count":0} ]
  );

  await TestCoverage(
    "yield expressions (.return and .throw)",
    `
const it0 = function*() {                 // 0000
  yield 1; yield 2; yield 3;              // 0050
}();                                      // 0100
it0.next(); it0.return();                 // 0150
try {                                     // 0200
  const it1 = function*() {               // 0250
    yield 1; yield 2; yield 3;            // 0300
  }();                                    // 0350
  it1.next(); it1.throw();                // 0400
} catch (e) {}                            // 0450
    `,
    [ {"start":0,"end":499,"count":1},
      {"start":12,"end":101,"count":1},
      {"start":60,"end":100,"count":0},
      {"start":264,"end":353,"count":1},
      {"start":312,"end":352,"count":0} ]
  );

  await TestCoverage(
    "yield expressions (.return and try/catch/finally)",
    `
const it = function*() {                  // 0000
  try {                                   // 0050
    yield 1; yield 2; yield 3;            // 0100
  } catch (e) {                           // 0150
    nop();                                // 0200
  } finally { nop(); }                    // 0250
  yield 4;                                // 0300
}();                                      // 0350
it.next(); it.return();                   // 0450
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":11,"end":351,"count":1},
      {"start":112,"end":254,"count":0},
      {"start":272,"end":350,"count":0} ]
  );

  await TestCoverage(
    "yield expressions (.throw and try/catch/finally)",
    `
const it = function*() {                  // 0000
  try {                                   // 0050
    yield 1; yield 2; yield 3;            // 0100
  } catch (e) {                           // 0150
    nop();                                // 0200
  } finally { nop(); }                    // 0250
  yield 4;                                // 0300
}();                                      // 0350
it.next(); it.throw(42);                  // 0550
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":11,"end":351,"count":1},
      {"start":112,"end":154,"count":0},
      {"start":310,"end":350,"count":0} ]
  );

  await TestCoverage(
    "yield* expressions",
    `
const it = function*() {                  // 0000
  yield* gen();                           // 0050
  yield* nop() ? gen() : gen()            // 0100
  return gen();                           // 0150
}();                                      // 0200
it.next(); it.next(); it.next();          // 0250
it.next(); it.next(); it.next();          // 0300
    `,
    [ {"start":0,"end":349,"count":1},
      {"start":11,"end":201,"count":1},
      {"start":115,"end":122,"count":0},
      {"start":130,"end":200,"count":0} ]
  );

  await TestCoverage(
    "yield* expressions (.return and .throw)",
    `
const it0 = function*() {                 // 0000
  yield* gen(); yield* gen(); yield 3;    // 0050
}();                                      // 0100
it0.next(); it0.return();                 // 0150
try {                                     // 0200
  const it1 = function*() {               // 0250
    yield* gen(); yield* gen(); yield 3;  // 0300
  }();                                    // 0350
  it1.next(); it1.throw();                // 0400
} catch (e) {}                            // 0450
    `,
    [ {"start":0,"end":499,"count":1},
      {"start":12,"end":101,"count":1},
      {"start":65,"end":100,"count":0},
      {"start":264,"end":353,"count":1},
      {"start":317,"end":352,"count":0} ]
  );

  await TestCoverage(
    "LogicalOrExpression assignment",
    `
const a = true || 99                      // 0000
function b () {                           // 0050
  const b = a || 2                        // 0100
}                                         // 0150
b()                                       // 0200
b()                                       // 0250
    `,
    [ {"start":0,"end":299,"count":1},
      {"start":15,"end":20,"count":0},
      {"start":50,"end":151,"count":2},
      {"start":114,"end":118,"count":0} ]
  );

  await TestCoverage(
    "LogicalOrExpression IsTest()",
    `
true || false                             // 0000
const a = 99                              // 0050
a || 50                                   // 0100
const b = false                           // 0150
if (b || true) {}                         // 0200
    `,
    [ {"start":0,"end":249,"count":1},
      {"start":5,"end":13,"count":0},
      {"start":102,"end":107,"count":0} ]
  );

  await TestCoverage(
    "LogicalAndExpression assignment",
    `
const a = false && 99                     // 0000
function b () {                           // 0050
  const b = a && 2                        // 0100
}                                         // 0150
b()                                       // 0200
b()                                       // 0250
const c = true && 50                      // 0300
    `,
    [ {"start":0,"end":349,"count":1},
      {"start":16,"end":21,"count":0},
      {"start":50,"end":151,"count":2},
      {"start":114,"end":118,"count":0} ]
  );

  await TestCoverage(
    "LogicalAndExpression IsTest()",
    `
false && true                             // 0000
const a = 0                               // 0050
a && 50                                   // 0100
const b = true                            // 0150
if (b && true) {}                         // 0200
true && true                              // 0250
    `,
    [ {"start":0,"end":299,"count":1},
      {"start":6,"end":13,"count":0},
      {"start":102,"end":107,"count":0} ]
  );

  await TestCoverage(
    "NaryLogicalOr assignment",
    `
const a = true                            // 0000
const b = false                           // 0050
const c = false || false || 99            // 0100
const d = false || true || 99             // 0150
const e = true || true || 99              // 0200
const f = b || b || 99                    // 0250
const g = b || a || 99                    // 0300
const h = a || a || 99                    // 0350
const i = a || (b || c) || d              // 0400
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":174,"end":179,"count":0},
      {"start":215,"end":222,"count":0},
      {"start":223,"end":228,"count":0},
      {"start":317,"end":322,"count":0},
      {"start":362,"end":366,"count":0},
      {"start":367,"end":372,"count":0},
      {"start":412,"end":423,"count":0},
      {"start":424,"end":428,"count":0} ]
  );

  await TestCoverage(
    "NaryLogicalOr IsTest()",
    `
const a = true                            // 0000
const b = false                           // 0050
false || false || 99                      // 0100
false || true || 99                       // 0150
true || true || 99                        // 0200
b || b || 99                              // 0250
b || a || 99                              // 0300
a || a || 99                              // 0350
    `,
    [ {"start":0,"end":399,"count":1},
      {"start":164,"end":169,"count":0},
      {"start":205,"end":212,"count":0},
      {"start":213,"end":218,"count":0},
      {"start":307,"end":312,"count":0},
      {"start":352,"end":356,"count":0},
      {"start":357,"end":362,"count":0} ]
  );

  await TestCoverage(
    "NaryLogicalAnd assignment",
    `
const a = true                            // 0000
const b = false                           // 0050
const c = false && false && 99            // 0100
const d = false && true && 99             // 0150
const e = true && true && 99              // 0200
const f = true && false || true           // 0250
const g = true || false && true           // 0300
    `,
    [ {"start":0,"end":349,"count":1},
      {"start":116,"end":124,"count":0},
      {"start":125,"end":130,"count":0},
      {"start":166,"end":173,"count":0},
      {"start":174,"end":179,"count":0},
      {"start":315,"end":331,"count":0}
  ]);

  await TestCoverage(
    "NaryLogicalAnd IsTest()",
    `
const a = true                            // 0000
const b = false                           // 0050
false && false && 99                      // 0100
false && true && 99                       // 0150
true && true && 99                        // 0200
true && false || true                     // 0250
true || false && true                     // 0300
false || false || 99 || 55                // 0350
    `,
    [ {"start":0,"end":399,"count":1},
      {"start":106,"end":114,"count":0},
      {"start":115,"end":120,"count":0},
      {"start":156,"end":163,"count":0},
      {"start":164,"end":169,"count":0},
      {"start":305,"end":321,"count":0},
      {"start":371,"end":376,"count":0} ]
  );

  // see regression: https://crbug.com/785778
  await TestCoverage(
    "logical expressions + conditional expressions",
    `
const a = true                            // 0000
const b = 99                              // 0050
const c = false                           // 0100
const d = ''                              // 0150
const e = a && (b ? 'left' : 'right')     // 0200
const f = a || (b ? 'left' : 'right')     // 0250
const g = c || d ? 'left' : 'right'       // 0300
const h = a && b && (b ? 'left' : 'right')// 0350
const i = d || c || (c ? 'left' : 'right')// 0400
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":227,"end":236,"count":0},
      {"start":262,"end":287,"count":0},
      {"start":317,"end":325,"count":0},
      {"start":382,"end":391,"count":0},
      {"start":423,"end":431,"count":0} ]
  );

  await TestCoverage(
    "https://crbug.com/827530",
    `
Util = {};                                // 0000
Util.escape = function UtilEscape(str) {  // 0050
  if (!str) {                             // 0100
    return 'if';                          // 0150
  } else {                                // 0200
    return 'else';                        // 0250
  }                                       // 0300
};                                        // 0350
Util.escape("foo.bar");                   // 0400
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":64,"end":351,"count":1},
      {"start":112,"end":203,"count":0} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/8237",
    `
!function() {                             // 0000
  if (true)                               // 0050
    while (false) return; else nop();     // 0100
}();                                      // 0150
!function() {                             // 0200
  if (true) l0: { break l0; } else        // 0250
    if (nop()) { }                        // 0300
}();                                      // 0350
!function() {                             // 0400
  if (true) { if (false) { return; }      // 0450
  } else if (nop()) { } }();              // 0500
!function(){                              // 0550
  if(true)while(false)return;else nop()   // 0600
}();                                      // 0650
!function(){                              // 0700
  if(true) l0:{break l0}else if (nop()){} // 0750
}();                                      // 0800
!function(){                              // 0850
  if(true){if(false){return}}else         // 0900
    if(nop()){}                           // 0950
}();                                      // 1000
    `,
    [ {"start":0,"end":1049,"count":1},
      {"start":1,"end":151,"count":1},
      {"start":118,"end":137,"count":0},
      {"start":201,"end":351,"count":1},
      {"start":279,"end":318,"count":0},
      {"start":401,"end":525,"count":1},
      {"start":475,"end":486,"count":0},
      {"start":503,"end":523,"count":0},
      {"start":551,"end":651,"count":1},
      {"start":622,"end":639,"count":0},
      {"start":701,"end":801,"count":1},
      {"start":774,"end":791,"count":0},
      {"start":851,"end":1001,"count":1},
      {"start":920,"end":928,"count":0},
      {"start":929,"end":965,"count":0} ]
  );

  await TestCoverage(
    "terminal break statement",
    `
while (true) {                            // 0000
  const b = false                         // 0050
  break                                   // 0100
}                                         // 0150
let stop = false                          // 0200
while (true) {                            // 0250
  if (stop) {                             // 0300
    break                                 // 0350
  }                                       // 0400
  stop = true                             // 0450
}                                         // 0500
    `,
    [ {"start":0,"end":549,"count":1},
      {"start":263,"end":501,"count":2},
      {"start":312,"end":501,"count":1} ]
  );

  await TestCoverage(
    "terminal return statement",
    `
function a () {                           // 0000
  const b = false                         // 0050
  return 1                                // 0100
}                                         // 0150
const b = (early) => {                    // 0200
  if (early) {                            // 0250
    return 2                              // 0300
  }                                       // 0350
  return 3                                // 0400
}                                         // 0450
const c = () => {                         // 0500
  if (true) {                             // 0550
    return                                // 0600
  }                                       // 0650
}                                         // 0700
a(); b(false); b(true); c()               // 0750
    `,
    [ {"start":0,"end":799,"count":1},
      {"start":0,"end":151,"count":1},
      {"start":210,"end":451,"count":2},
      {"start":263,"end":450,"count":1},
      {"start":510,"end":701,"count":1} ]
  );

  await TestCoverage(
    "terminal blocks",
    `
function a () {                           // 0000
  {                                       // 0050
    return 'a'                            // 0100
  }                                       // 0150
}                                         // 0200
function b () {                           // 0250
  {                                       // 0300
    {                                     // 0350
      return 'b'                          // 0400
    }                                     // 0450
  }                                       // 0500
}                                         // 0550
a(); b()                                  // 0600
    `,
    [ {"start":0,"end":649,"count":1},
      {"start":0,"end":201,"count":1},
      {"start":250,"end":551,"count":1} ]
  );

  await TestCoverage(
    "terminal if statements",
    `
function a (branch) {                     // 0000
  if (branch) {                           // 0050
    return 'a'                            // 0100
  } else {                                // 0150
    return 'b'                            // 0200
  }                                       // 0250
}                                         // 0300
function b (branch) {                     // 0350
  if (branch) {                           // 0400
    if (branch) {                         // 0450
      return 'c'                          // 0500
    }                                     // 0550
  }                                       // 0600
}                                         // 0650
function c (branch) {                     // 0700
  if (branch) {                           // 0750
    return 'c'                            // 0800
  } else {                                // 0850
    return 'd'                            // 0900
  }                                       // 0950
}                                         // 1000
function d (branch) {                     // 1050
  if (branch) {                           // 1100
    if (!branch) {                        // 1150
      return 'e'                          // 1200
    } else {                              // 1250
      return 'f'                          // 1300
    }                                     // 1350
  } else {                                // 1400
    // noop                               // 1450
  }                                       // 1500
}                                         // 1550
a(true); a(false); b(true); b(false)      // 1600
c(true); d(true);                         // 1650
    `,
    [ {"start":0,"end":1699,"count":1},
      {"start":0,"end":301,"count":2},
      {"start":64,"end":253,"count":1},
      {"start":350,"end":651,"count":2},
      {"start":414,"end":603,"count":1},
      {"start":700,"end":1001,"count":1},
      {"start":853,"end":953,"count":0},
      {"start":1050,"end":1551,"count":1},
      {"start":1167,"end":1255,"count":0},
      {"start":1403,"end":1503,"count":0} ]
  );

  await TestCoverage(
    "https://crbug.com/927464",
    `
!function f() {                           // 0000
  function unused() { nop(); }            // 0050
  nop();                                  // 0100
}();                                      // 0150
    `,
    [ {"start":0,"end":199,"count":1},
      {"start":1,"end":151,"count":1},
      {"start":52,"end":80,"count":0} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/8691",
    `
function f(shouldThrow) {                 // 0000
  if (shouldThrow) {                      // 0050
    throw Error('threw')                  // 0100
  }                                       // 0150
}                                         // 0200
try {                                     // 0250
  f(true)                                 // 0300
} catch (err) {                           // 0350
                                          // 0400
}                                         // 0450
try {                                     // 0500
  f(false)                                // 0550
} catch (err) {}                          // 0600
    `,
    [ {"start":0,"end":649,"count":1},
      {"start":602,"end":616,"count":0},
      {"start":0,"end":201,"count":2},
      {"start":69,"end":153,"count":1} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/9705",
    `
function f(x) {                           // 0000
  switch (x) {                            // 0050
    case 40: nop();                       // 0100
    case 41: nop(); return 1;             // 0150
    case 42: nop(); break;                // 0200
  }                                       // 0250
  return 3;                               // 0300
};                                        // 0350
f(40);                                    // 0400
f(41);                                    // 0450
f(42);                                    // 0500
f(43);                                    // 0550
    `,
    [ {"start":0,"end":599,"count":1},
      {"start":0,"end":351,"count":4},
      {"start":104,"end":119,"count":1},
      {"start":154,"end":179,"count":2},
      {"start":204,"end":226,"count":1},
      {"start":253,"end":350,"count":2} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/9705",
    `
function f(x) {                           // 0000
  switch (x) {                            // 0050
    case 40: nop();                       // 0100
    case 41: nop(); return 1;             // 0150
    case 42: nop(); break;                // 0200
  }                                       // 0250
  return 3;                               // 0300
};                                        // 0350
f(42);                                    // 0400
f(43);                                    // 0450
    `,
    [ {"start":0,"end":499,"count":1},
      {"start":0,"end":351,"count":2},
      {"start":104,"end":119,"count":0},
      {"start":154,"end":179,"count":0},
      {"start":204,"end":226,"count":1} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/9857",
    `function foo() {}`,
    [ {"start":0,"end":17,"count":1},
      {"start":0,"end":17,"count":0} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/9857",
    `function foo() {function bar() {}}; foo()`,
    [ {"start":0,"end":41,"count":1},
      {"start":0,"end":34,"count":1},
      {"start":16,"end":33,"count":0} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/9952",
    `
function test(foo = "foodef") {           // 0000
  return {bar};                           // 0050
                                          // 0100
  function bar() {                        // 0150
    console.log("test");                  // 0200
  }                                       // 0250
}                                         // 0300
test().bar();                             // 0350
    `,
    [ {"start":0,"end":399,"count":1},
      {"start":0,"end":301,"count":1},
      {"start":152,"end":253,"count":1} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/9952",
    `
function test(foo = (()=>{})) {           // 0000
  return {foo};                           // 0050
}                                         // 0100
                                          // 0150
test(()=>{}).foo();                       // 0200
    `,
    [ {"start":0,"end":249,"count":1},
      {"start":0,"end":101,"count":1},
      {"start":21,"end":27,"count":0},
      {"start":205,"end":211,"count":1} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/10030 - original",
    `
function a (shouldThrow) {                // 0000
  try {                                   // 0050
    if (shouldThrow)                      // 0100
      throw Error('I threw!');            // 0150
    return 'I ran';                       // 0200
  } catch(e) {                            // 0250
    console.info('caught');               // 0300
  }                                       // 0350
}                                         // 0400
a(false);                                 // 0450
a(true);                                  // 0500
    `,
    [ {"start":0,"end":549,"count":1},
      {"start":0,"end":401,"count":2},
      {"start":156,"end":353,"count":1} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/10030 - only throw",
    `
function a (shouldThrow) {                // 0000
  try {                                   // 0050
    if (shouldThrow)                      // 0100
      throw Error('I threw!');            // 0150
    return 'I ran';                       // 0200
  } catch(e) {                            // 0250
    console.info('caught');               // 0300
  }                                       // 0350
}                                         // 0400
a(true);                                  // 0450
    `,
    [ {"start":0,"end":499,"count":1},
      {"start":0,"end":401,"count":1},
      {"start":180,"end":254,"count":0} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/10030 - finally",
    `
function a (shouldThrow) {                // 0000
  try {                                   // 0050
    return 'I ran';                       // 0100
  } finally {                             // 0150
    console.info('finally');              // 0200
  }                                       // 0250
}                                         // 0300
a(false);                                 // 0350
a(true);                                  // 0400
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":0,"end":301,"count":2} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/10030 - catch & finally",
    `
function a (shouldThrow) {                // 0000
  try {                                   // 0050
    return 'I ran';                       // 0100
  } catch (e) {                           // 0150
    console.info('caught');               // 0200
  } finally {                             // 0250
    console.info('finally');              // 0300
  }                                       // 0350
}                                         // 0400
a(false);                                 // 0450
a(true);                                  // 0500
    `,
    [ {"start":0,"end":549,"count":1},
    {"start":0,"end":401,"count":2},
    {"start":154,"end":254,"count":0} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/11231 - nullish coalescing",
    `
const a = true                            // 0000
const b = false                           // 0050
const c = undefined                       // 0100
const d = a ?? 99                         // 0150
const e = 33                              // 0200
const f = b ?? (c ?? 99)                  // 0250
const g = 33                              // 0300
const h = c ?? (c ?? 'hello')             // 0350
const i = c ?? b ?? 'hello'               // 0400
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":162,"end":167,"count":0},
      {"start":262,"end":274,"count":0},
      {"start":417,"end":427,"count":0} ]
  );

  await TestCoverage(
    "Optional Chaining",
    `
const a = undefined || null               // 0000
const b = a?.b                            // 0050
const c = a?.['b']                        // 0100
const d = {                               // 0150
  e: {f: 99, g: () => {return undefined}} // 0200
}                                         // 0250
const e = d?.e?.f                         // 0300
const f = d?.e?.['f']                     // 0350
const g = d?.e?.f?.g                      // 0400
const h = d?.e?.f?.g?.h                   // 0450
const i = d?.['d']?.['e']?.['h']          // 0500
const k = a?.('b')                        // 0550
const l = d?.e?.g?.()                     // 0600
const m = d?.e?.g?.()?.a?.b               // 0650
delete a?.b                               // 0700
const n = d?.[d?.x?.f]                    // 0750
if (a?.[d?.x?.f]) { const p = 99 } else {}// 0800
const p = d?.[d?.x?.f]?.x                 // 0850
    `,
    [ {"start":0,"end":899,"count":1},
      {"start":61,"end":64,"count":0},
      {"start":111,"end":118,"count":0},
      {"start":470,"end":473,"count":0},
      {"start":518,"end":532,"count":0},
      {"start":561,"end":568,"count":0},
      {"start":671,"end":677,"count":0},
      {"start":708,"end":711,"count":0},
      {"start":768,"end":771,"count":0},
      {"start":805,"end":816,"count":0},
      {"start":818,"end":834,"count":0},
      {"start":868,"end":871,"count":0},
      {"start":872,"end":875,"count":0},
      {"start":216,"end":240,"count":2} ]
  );

  await TestCoverage(
      'using in a block', `
{ using x = {               // 0000
    value: 1,               // 0050
    [Symbol.dispose]() {    // 0100
      return 42;            // 0150
    } }; }                  // 0200
    `,
      [
        {'start': 0, 'end': 179, 'count': 1},
        {'start': 76, 'end': 149, 'count': 1}
      ]);

  %DebugToggleBlockCoverage(false);

})();
