// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-source-phase-imports --allow-natives-syntax

assertPromiseResult(import.source('../incrementer.wasm'), mod => {
  assertEquals(Object.getPrototypeOf(mod), WebAssembly.Module.prototype);
  assertEquals(mod[Symbol.toStringTag], 'WebAssembly.Module');

  var AbstractModuleSource = %GetAbstractModuleSource();
  var ToStringTag = Object
    .getOwnPropertyDescriptor(
      AbstractModuleSource.prototype,
      Symbol.toStringTag,
    ).get;
  assertTrue(mod instanceof AbstractModuleSource);
  assertEquals(ToStringTag.call(mod), "WebAssembly.Module");
}, assertUnreachable);
