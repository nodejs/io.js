// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --trace-wasm-memory --liftoff --no-wasm-tier-up

d8.file.execute("test/message/wasm-trace-memory64.js");
