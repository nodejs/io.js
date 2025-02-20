// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --fuzzing

assertTrue(typeof %WasmStruct() === "object");
assertTrue(typeof %WasmArray() === "object");

// Printing a wasm object causes a TypeError.
// In --jitless mode (needed for differential fuzzing), the wasm object is
// replaced with a frozen empty object, reproducing the same behavior).
assertThrows(() => print(%WasmStruct()), TypeError);
assertThrows(() => print(%WasmArray()), TypeError);
