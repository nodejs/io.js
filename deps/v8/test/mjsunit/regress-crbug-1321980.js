// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

try {
  // We're only testing these don't crash. It's platform-dependent which of them throw.
  new ArrayBuffer(1, {maxByteLength: 2147483647});
  new ArrayBuffer(1, {maxByteLength: 9007199254740000});
} catch (e) {

}
