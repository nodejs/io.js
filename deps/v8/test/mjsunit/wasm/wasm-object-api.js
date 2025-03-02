// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Old API should be gone.
assertEquals("undefined", typeof Wasm);

// New API should rule.
assertEquals('object', typeof WebAssembly);
assertEquals('function', typeof WebAssembly.Module);
assertEquals('function', typeof WebAssembly.Instance);
assertEquals('function', typeof WebAssembly.compile);
assertEquals('function', typeof WebAssembly.validate);
