// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function foo() {
  return {}.__defineGetter__();
}

%PrepareFunctionForOptimization(foo);
try { foo() } catch (e) {}
%OptimizeFunctionOnNextCall(foo);
try { foo() } catch (e) {}
