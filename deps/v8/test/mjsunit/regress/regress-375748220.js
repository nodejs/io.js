// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let error_message = "\\8 and \\9 are not allowed in strict mode.";

assertThrows(() => eval("(class extends ('\\8', Object) {})"), SyntaxError,
             error_message);

assertThrows(() => eval("(class extends Object { x = '\\8'; })"), SyntaxError,
             error_message);

assertThrows(() => eval("(class extends Object { static x = '\\8'; })"), SyntaxError,
             error_message);
