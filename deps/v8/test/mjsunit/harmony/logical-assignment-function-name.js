// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


{
  let x = undefined;
  x ??= function() {};

  assertEquals(x.name, "x");
}


{
  let y = false;
  y ||= function() {};

  assertEquals(y.name, "y");
}

{
  let z = true;
  z &&= function() {};

  assertEquals(z.name, "z");
}
