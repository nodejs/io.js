// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function create_regexp_lit() {
  return /abc/;
}

%PrepareFunctionForOptimization(create_regexp_lit);
assertEquals(/abc/, create_regexp_lit());
assertEquals(/abc/, create_regexp_lit());
%OptimizeFunctionOnNextCall(create_regexp_lit);
assertEquals(/abc/, create_regexp_lit());
assertOptimized(create_regexp_lit);
