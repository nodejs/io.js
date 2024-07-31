// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

assertTrue((new Intl.Collator("th")).resolvedOptions().ignorePunctuation);
assertFalse((new Intl.Collator("th", {ignorePunctuation: false})).resolvedOptions().ignorePunctuation);
assertTrue((new Intl.Collator("th", {ignorePunctuation: true})).resolvedOptions().ignorePunctuation);
