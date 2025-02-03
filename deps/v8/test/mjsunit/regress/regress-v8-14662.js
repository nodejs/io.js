// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --stress-snapshot

const realm = Realm.create();
var object = Realm.eval(realm, "Object");
