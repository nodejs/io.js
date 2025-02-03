// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

delete WebAssembly.Table.prototype.type;
WebAssembly.Table.prototype.type = {};

delete WebAssembly.Global.prototype.type;
WebAssembly.Global.prototype.type = {};

delete WebAssembly.Memory.prototype.type;
WebAssembly.Memory.prototype.type = {};

delete WebAssembly.Tag.prototype.type;
WebAssembly.Tag.prototype.type = {};

d8.test.enableJSPI();
