// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

assertEquals("¥1.235E7",
    (new Intl.NumberFormat('en', {style: "currency", currency: "JPY", notation: "scientific"})).format(12345678));

assertEquals("$1.235E7",
    (new Intl.NumberFormat('en', {style: "currency", currency: "USD", notation: "scientific"})).format(12345678));

assertEquals("¥12.346E6",
    (new Intl.NumberFormat('en', {style: "currency", currency: "JPY", notation: "engineering"})).format(12345678));

assertEquals("$12.346E6",
    (new Intl.NumberFormat('en', {style: "currency", currency: "USD", notation: "engineering"})).format(12345678));
