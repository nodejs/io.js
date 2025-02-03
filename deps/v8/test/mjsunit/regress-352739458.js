// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --reuse-scope-infos --allow-natives-syntax

Realm.eval(Realm.current(), `function f(s) {
  return eval(s);
}
let g = f("0,function(y) { return ()=>{ eval() } }");
let i = g(1);
%ForceFlush(g);
print(g(2));`);
