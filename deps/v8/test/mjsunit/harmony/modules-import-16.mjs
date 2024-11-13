// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

globalThis.ran = false;
globalThis.x = undefined;

let body = "import('modules-skip-1.mjs').then(ns => { x = ns.life();" +
    " ran = true;} ).catch(err => %AbortJS(err.toString()))"
let func = new Function(body);
func();

%PerformMicrotaskCheckpoint();
assertEquals(42, globalThis.x);
assertTrue(globalThis.ran);

globalThis.ran = false;
body = "import('modules-skip-1.mjs').then(ns => { x = ns.life();" +
    " ran = true;} ).catch(err => %AbortJS(err.toString()))"
eval("var func = new Function(body); func();");

%PerformMicrotaskCheckpoint();
assertEquals(42, globalThis.x);
assertTrue(globalThis.ran);

globalThis.ran = false;
body = "eval(import('modules-skip-1.mjs').then(ns => { x = ns.life();" +
    " ran = true;} ).catch(err => %AbortJS(err.toString())))"
func = new Function(body);
func();

%PerformMicrotaskCheckpoint();
assertEquals(42, globalThis.x);
assertTrue(globalThis.ran);
