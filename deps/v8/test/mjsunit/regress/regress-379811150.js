// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function __f_0() {}
%PrepareFunctionForOptimization(__f_0);
%OptimizeFunctionOnNextCall(__f_0);
%OptimizeFunctionOnNextCall(__f_0, "concurrent");
%OptimizeFunctionOnNextCall(__f_0);
__f_0();
%OptimizeFunctionOnNextCall(__f_0);
%OptimizeFunctionOnNextCall(__f_0, "concurrent");
%OptimizeFunctionOnNextCall(__f_0);
