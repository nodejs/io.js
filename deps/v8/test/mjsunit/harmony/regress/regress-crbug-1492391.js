// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const v1 = Array.fromAsync;
const v2 = [-6.752893033403886];
const o3 = {
};
Object.defineProperty(o3, "type", { configurable: true, enumerable: true, set: v1 });
o3.type = v2;
