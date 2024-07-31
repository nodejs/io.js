// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

assertTrue(%StringWrapperToPrimitiveProtector());

let x = new String('old');

delete String.prototype.valueOf;

assertFalse(%StringWrapperToPrimitiveProtector());

// If valueOf doesn't exist, then toString is used.
String.prototype.toString = function() { return 'new'; };

assertEquals('got new', 'got ' + x);
