// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
{
  let v1 = 1;
  for (let i2 = v1;i2 >= v1 &&i2 < 10000000000; i2 += v1) {
    const v9 = v1 && i2;
    ~v9;
    v1 = v9;
  }
}
