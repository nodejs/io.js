// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var r = Realm.create();
for (let i = 0; i < 3; i++) {
  var dst = {};
  var src = Realm.eval(r, "var o = {}; o.x = 1; o");
  Object.assign(dst, src);
}
