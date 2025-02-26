// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --use-std-math-pow

assertEquals(1, (Math.pow(NaN, 0)));
assertEquals(1, (Math.pow(new Float64Array(new Uint32Array([0x00000001,0x7FF00000]).buffer)[0], 0)));
