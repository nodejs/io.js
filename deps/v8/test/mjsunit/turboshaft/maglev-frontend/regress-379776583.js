// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev

function bar(n) {
  if (n == 0) return;
  if (n == 1) Throw();
  if (n == 2) foo(n-2);
}
%NeverOptimizeFunction(bar);

function foo(n) {
  // This first try should not throw during feedback collection and thus be
  // kLazyDeoptOnThrow.
  try { bar(n); } catch(e) {}

  // This second try should throw during feedback collection, so that it's not
  // kLazyDeoptOnThrow.
  try { bar(n+1); } finally {}
}

// Let's call the 2 lines of `foo` L1 and L2.
// foo(0) does:
//   - in L1, `bar(0)` just returns
//   - then, in L2, `bar(1)` just Throws
//
// foo(2) does:
//   - in L1, `bar(2)` calls `foo(0)`;
//     cf above for what this does (this will throw and should
//     be caught by the try-catch in L1, but this is
//     kLazyDeoptOnThrow, so it should deopt).

%PrepareFunctionForOptimization(foo);
try { foo(0); } catch(e) {}

%OptimizeFunctionOnNextCall(foo);
try { foo(0); } catch(e) {}
try { foo(2); } catch(e) {}
