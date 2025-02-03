// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --nolazy

({a})=>{{let a = 10; return ()=>a}; let b = 10}
