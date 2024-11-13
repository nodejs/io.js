// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function f() {}
var a = Array(2 ** 16 - 2);  // Elements in large-object-space.
f.bind(...a);
