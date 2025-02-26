// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

assertTrue(Number.MIN_VALUE * 2 > 0);
d8.test.setFlushDenormals(true);
assertFalse(Number.MIN_VALUE * 2 > 0);
assertEquals(Number.MIN_VALUE * 2, 0);
d8.test.setFlushDenormals(false);
assertTrue(Number.MIN_VALUE * 2 > 0);
