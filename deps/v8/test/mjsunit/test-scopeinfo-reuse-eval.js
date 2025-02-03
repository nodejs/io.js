// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

let x = 1;
let f = eval("let y = 20; (function() { y; return function() { return x } })");
// Keep g in f alive to keep its outer scope chain alive.
let g = f();
// Flush and recompile f, which will try to reuse the scope info chain attached
// to g. Verify that we don't get a clash between the eval scope and the script
// scope.
%ForceFlush(f);
f();
