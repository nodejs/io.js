// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let v1 = Realm.createAllowCrossRealmAccess();
const v2 = v1--;
const v4 = Realm.dispose(v2);
const v6 = this.performance;
v6.measureMemory();
