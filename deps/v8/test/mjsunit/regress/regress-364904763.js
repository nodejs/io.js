// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

assertEquals(
    '123xHelloxworld', '123\u00a0Hello\n \nworld'.replace(/\s+/g, 'x'));
assertEquals(
    '123xHelloxworld', '123\u00a0Hello\n \nworld'.replace(/\s+/g, 'x'));
