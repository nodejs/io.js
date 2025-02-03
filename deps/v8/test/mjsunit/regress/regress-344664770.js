// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function __f_4() {
    return arguments;
}

arr = [];
arr.length = 65535;

assertThrows(() => __f_4.bind(null,...arr),
             RangeError,
             "Too many arguments in function call (only 65535 allowed)");
