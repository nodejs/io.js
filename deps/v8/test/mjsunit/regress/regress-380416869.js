// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const r = RegExp(("()?").repeat(32767));
assertThrows(() => ("").match(/(?:(?=g)).{2147483648,}/ + r));
assertThrows(() => ("").match(/(?:(?=g)).{2147483648,}/ + r));
