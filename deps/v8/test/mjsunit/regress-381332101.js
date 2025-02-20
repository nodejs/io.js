// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var hash = 0;

let slot = [1 /
  - -1 / 0, 0 / - - - - - - - - - - - - -2147483904.0, - - - - - - -
  - -192446.0, - - - - - - - -15.0, - - - -4.60374e-07, - - - -
  1.48084e-15, - -1.05755e-19, -3.2995e-21, -1.67354e-23, -
  1.11885e-23, - - -1.27126e-38, -3e-88, -2e66, 0.0, 1.17549e-38, ,
  1.5707963267948966 -
  1073741824, 9.48298e-08, 0.244326, 1.0, 12.4895, 19.0, 47.0,
  538.324, 564.536, , , 2147483648.0, 4294967296.0, 1.51116e+11,
  4.18193e+13, 3.59167e+16, 9223372036854775808.0
];

function update(new_slot) {
  slot = new_slot;
  hash = slot + hash.toString();
}

function create_update() {
  return eval('(' + update.toString() + ')');
}

let vals = [1, -0.5, 0, -0]
for (_ of slot) {
  for (let v = 0; v < vals.length; v++)
    create_update()(vals[v]);
}

assertEquals(
"00-0.5100-0.5100-0.5100-0.5100-0.5100-0.5100-0.5100-0.5100-0.5100\
-0.5100-0.5100-0.5100-0.5100-0.5100-0.5100-0.5100-0.5100-0.5100-0.\
5100-0.5100-0.5100-0.5100-0.5100-0.5100-0.5100-0.5100-0.5100-0.510\
0-0.5100-0.5100-0.5100-0.5100-0.510", hash);
