// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


assertTrue(/[a-cB]/vi.test('b'));
assertTrue(/[a-cB]/vi.test('B'));
