// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const long_string = "abcdefghijklmnopqrstuvwxyz";
let slice0 = %ConstructSlicedString(long_string, 1);
let slice1 = %ConstructSlicedString(long_string, 2);
let re = /a/g;
slice0.match(re);
%CollectGarbage("");
slice1.match(re);
