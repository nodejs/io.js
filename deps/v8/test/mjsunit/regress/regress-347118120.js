// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

assertThrows(() => {
  %DefineObjectOwnProperty(null, 'foo', 1);
}, TypeError, /Cannot set properties of null \(setting 'foo'\)/);

assertThrows(() => {
  %DefineObjectOwnProperty(null, { toString() { return 'foo' } }, 1);
}, TypeError, /Cannot set properties of null/);
