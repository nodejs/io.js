// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function TestMemoryBufferNotResizable() {
  const m = new WebAssembly.Memory({
    initial: 128
  });

  assertFalse(m.buffer.resizable);
  // For non-resizable buffers, maxByteLength returns byteLength.
  assertEquals(m.buffer.maxByteLength, m.buffer.byteLength);
})();
