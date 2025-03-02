// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

assertTrue(/(?-i:a)|b/i.test("B"));
assertTrue(/(?-i:a)|b/i.test("B"));

let r1 = /(?i:a)|b/;
assertFalse(r1.test('B'));

let r2 = /(?i:c)|d/;
for (var i = 0; i < 100; i++) { r2.test('C') }
assertFalse(r2.test('D'));
