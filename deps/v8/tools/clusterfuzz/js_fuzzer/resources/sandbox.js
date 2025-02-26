// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Stub for V8's memory-corruption API.

try {
  Sandbox;
} catch (e) {
  this.Sandbox = {
    MemoryView: class MemoryView extends ArrayBuffer{},
    base: undefined,
    byteLength: 42,
    getAddressOf: () => 0,
    getObjectAt: () => undefined,
    getSizeOf: () => 42,
    getSizeOfObjectAt: () => 42,
    isValidObjectAt: () => true,
    isWritable: () => true,
    isWritableObjectAt: () => true,
    getInstanceTypeOf: () => undefined,
    getInstanceTypeOfObjectAt: () => undefined,
    getInstanceTypeIdOf: () => undefined,
    getInstanceTypeIdOfObjectAt: () => undefined,
    getInstanceTypeIdFor: () => 0,
    getFieldOffset: () => 31,
  };
}
