// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var __v_0 = function () {};
__v_0.prototype = undefined;
__v_0.x = 23;
__v_0.prototype = new ArrayBuffer();
__v_0.x = 2147483649;
assertDoesNotThrow("__v_0.prototype.add = null");
