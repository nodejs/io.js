// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

class C0 {
  constructor() {
      try { new  C0; } catch (e) {}
      const v5 = %WasmStruct();
  }
}
new C0;
