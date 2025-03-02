// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


const v2 = JSON.rawJSON(-2025014085);
const o3 = {
    "type": v2,
    __proto__: v2,
};
const v6 = [o3];
Reflect.apply(JSON.stringify, this, v6);
