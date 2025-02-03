// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

assertTrue(%StringWrapperToPrimitiveProtector());

// Setting toPrimitive on a string primitive doesn't invalidated the protector
// and also doesn't change the concatenation result.
let x = "old";
x[Symbol.toPrimitive] = () => { return 'new'; }

assertTrue(%StringWrapperToPrimitiveProtector());

assertEquals('got old', 'got ' + x);
