// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-always-turbofan --no-stress-flush-code
// Flags: --no-stress-incremental-marking
// Flags: --expose-gc --js-staging
// Files: test/mjsunit/code-coverage-utils.js

(async function () {

  %DebugToggleBlockCoverage(true);

  await TestCoverage(
    "await expressions",
    `
async function f() {                      // 0000
  await 42;                               // 0050
  await 42;                               // 0100
};                                        // 0150
f();                                      // 0200
%PerformMicrotaskCheckpoint();            // 0250
    `,
    [ {"start":0,"end":299,"count":1},
      {"start":0,"end":151,"count":1} ]
  );

  await TestCoverage(
    "for-await-of statements",
    `
!async function() {                       // 0000
  for await (var x of [0,1,2,3]) {        // 0050
    nop();                                // 0100
  }                                       // 0150
}();                                      // 0200
%PerformMicrotaskCheckpoint();            // 0250
    `,
    [ {"start":0,"end":299,"count":1},
      {"start":1,"end":201,"count":1},
      {"start":83,"end":153,"count":4} ]
  );

  await TestCoverage(
    "https://crbug.com/981313",
    `
class Foo {                               // 0000
  async timeout() {                       // 0000
    return new Promise(                   // 0100
        (r) => setTimeout(r, 10));        // 0000
  }                                       // 0200
}                                         // 0000
new Foo().timeout();                      // 0300
    `,
    [ {"start":0,  "end":349, "count":1},
      {"start":52, "end":203, "count":1},
      {"start":158,"end":182, "count":1} ]
  );

  await TestCoverage(
    "test async generator coverage",
    `
class Foo {                               // 0000
  async *timeout() {                      // 0000
    return new Promise(                   // 0100
        (r) => setTimeout(r, 10));        // 0000
  }                                       // 0200
}                                         // 0000
new Foo().timeout();                      // 0300
    `,
    [ {"start":0,  "end":349, "count":1},
      {"start":52, "end":203, "count":1},
      {"start":158,"end":182, "count":0} ]
  );

  await TestCoverage(
    "test async generator coverage with next call",
    `
class Foo {                               // 0000
  async *timeout() {                      // 0000
    return new Promise(                   // 0100
        (r) => setTimeout(r, 10));        // 0000
  }                                       // 0200
}                                         // 0000
new Foo().timeout().next();               // 0300
    `,
    [ {"start":0,  "end":349, "count":1},
      {"start":52, "end":203, "count":1},
      {"start":158,"end":182, "count":1} ]
  );

  await TestCoverage(
    "test two consecutive returns",
    `
class Foo {                               // 0000
  timeout() {                             // 0000
    return new Promise(                   // 0100
        (r) => setTimeout(r, 10));        // 0000
    return new Promise(                   // 0200
        (r) => setTimeout(r, 10));        // 0000
  }                                       // 0300
}                                         // 0000
new Foo().timeout();                      // 0400
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":52,"end":303,"count":1},
      {"start":184,"end":302,"count":0},
      {"start":158,"end":182,"count":1} ]
  );


  await TestCoverage(
    "test async generator with two consecutive returns",
    `
class Foo {                               // 0000
  async *timeout() {                      // 0000
    return new Promise(                   // 0100
        (r) => setTimeout(r, 10));        // 0000
    return new Promise(                   // 0200
        (r) => setTimeout(r, 10));        // 0000
  }                                       // 0300
}                                         // 0000
new Foo().timeout().next();               // 0400
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":52,"end":303,"count":1},
      {"start":184,"end":302,"count":0},
      {"start":158,"end":182,"count":1} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/9952",
    `
async function test(foo) {                // 0000
  return {bar};                           // 0050
                                          // 0100
  function bar() {                        // 0150
    console.log("test");                  // 0200
  }                                       // 0250
}                                         // 0300
test().then(r => r.bar());                // 0350
%PerformMicrotaskCheckpoint();            // 0400
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":0,"end":301,"count":1},
      {"start":152,"end":253,"count":1},
      {"start":362,"end":374,"count":1} ]
  );

  await TestCoverage(
    "https://crbug.com/v8/10628",
    `
async function abc() {                    // 0000
 try {                                    // 0050
  return 'abc';                           // 0100
 } finally {                              // 0150
  console.log('in finally');              // 0200
 }                                        // 0250
}                                         // 0300
abc();                                    // 0350
%PerformMicrotaskCheckpoint();            // 0400
    `,
    [ {"start":0,"end":449,"count":1},
      {"start":0,"end":301,"count":1} ]
  );

  await TestCoverage(
    "try/catch/finally statements async",
    `
!async function() {                       // 0000
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
    "try/catch/finally statements with early return async",
    `
!async function() {                       // 0000
  try { throw 42; } catch (e) { return; } // 0050
  nop();                                  // 0100
}();                                      // 0150
!async function() {                       // 0200
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
      'await using in a block', `
async function testAwaitUsing() {               // 0000
    await using x = {                           // 0050
    value: 1,                                   // 0100
    [Symbol.asyncDispose]() {                   // 0150
      return 42;                                // 0200
    } };                                        // 0250
  }                                             // 0300
  testAwaitUsing();                             // 0350
  %PerformMicrotaskCheckpoint();                // 0400
    `,
      [
        {'start': 0, 'end': 503, 'count': 1},
        {'start': 0, 'end': 339, 'count': 1},
        {'start': 172, 'end': 285, 'count': 1}
      ]);

  %DebugToggleBlockCoverage(false);

})();
