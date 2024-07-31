// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let empty_module = Uint8Array.from([0, 97, 115, 109, 1, 0, 0, 0]);
new WebAssembly.Instance(new WebAssembly.Module(empty_module));
