// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Attempts to explicitly load mjsunit should be ignored.
d8.file.execute("test/mjsunit/mjsunit.js");

// This code should be inlined.
d8.file.execute("test/mjsunit/wmb.js");
