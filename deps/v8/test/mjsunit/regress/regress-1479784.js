// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const re = /(x)(x)(x)(x)(x)(x)(x)(x)|/g;
assertEquals("".replace(re, () => 42), "42");
