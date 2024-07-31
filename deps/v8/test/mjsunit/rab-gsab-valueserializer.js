// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

(function FlagMismatch() {
  // Length tracking TA, buffer not resizable.
  const data1 = new Uint8Array([255, 15, 66, 4, 3, 5, 7, 11, 86, 66, 1, 2, 1]);
  assertThrows(() => { d8.serializer.deserialize(data1.buffer); });

  // RAB backed TA, buffer not resizable.
  const data2 = new Uint8Array([255, 15, 66, 4, 3, 5, 7, 11, 86, 66, 1, 2, 2]);
  assertThrows(() => { d8.serializer.deserialize(data2.buffer); });
})();
