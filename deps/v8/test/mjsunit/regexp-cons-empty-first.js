// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --regexp-interpret-all

// TF-style cons strings (with an empty `first`) are flattened:
let s = "foo";
s.replace(/x.z/g, "");  // Prime the regexp.
s = %ConstructConsString("aaaaaaaaaaaaaa", "bbbbbbbbbbbbbbc");
s = %ConstructConsString("", s);
s = s.replace(/x.z/g, "");
