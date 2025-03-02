// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


{
  let x = null;
  let y = 0;
  x ??= y ||= 5;

  assertEquals(x, 5);
  assertEquals(y, 5);
}


{
  let x = null;
  let y = 4;
  x ??= y ||= 5;

  assertEquals(x, 4);
  assertEquals(y, 4);
}

{
  let x = 1;
  let y = 0;
  x &&= y ||= 5;

  assertEquals(x, 5);
  assertEquals(y, 5);
}

{
  let x = 0;
  let y = 0;
  x &&= y ||= 5;

  assertEquals(x, 0);
  assertEquals(y, 0);
}
