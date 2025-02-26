// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let df = new Intl.DurationFormat("en", {style: "digital"});
assertEquals("1234567:20:45", df.format({hours: 1234567, minutes: 20, seconds: 45}));
assertEquals("12:1234567:20", df.format({hours: 12, minutes: 1234567, seconds: 20}));
assertEquals("12:34:1234567", df.format({hours: 12, minutes: 34, seconds: 1234567}));
assertEquals("12:34:1290.567", df.format({hours: 12, minutes: 34, seconds: 56, milliseconds: 1234567}));
assertEquals("1,234,567 days, 3:20:45", df.format({days: 1234567, hours: 3, minutes: 20, seconds: 45}));
