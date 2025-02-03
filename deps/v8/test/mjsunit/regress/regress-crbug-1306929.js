// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const gsab = new SharedArrayBuffer(1024, {maxByteLength: 11337});
const ta = new Float64Array(gsab);
Object.defineProperty(ta, 0, {});
