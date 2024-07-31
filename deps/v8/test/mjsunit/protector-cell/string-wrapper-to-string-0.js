// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

assertTrue(%StringWrapperToPrimitiveProtector());

// Setting toString on a string wrapper doesn't invalidated the protector
// and also doesn't change the concatenation result.
let x = new String('old');
x.toString = () => { return 'new1'; }

// Neither does setting toString on String.prototype.
String.prototype.toString = () => { return 'new2'; }

// Neither does setting toString on Object.prototype.
Object.prototype.toString = () => { return 'new3'; }

assertTrue(%StringWrapperToPrimitiveProtector());

assertEquals('got old', 'got ' + x);
