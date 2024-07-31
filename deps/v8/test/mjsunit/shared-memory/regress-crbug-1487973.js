// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-externalize-string --shared-string-table
// Flags: --allow-natives-syntax

function CheckExternal() {
  let o = {};
  const key = "25964";
  externalizeString(key);
  o[key] = 'foo';
  assertTrue(Reflect.has(o, key));
}

CheckExternal();

function CheckInternalizedExternal() {
  let o = {};
  const key = createExternalizableString("259" + "63");
  const shared_key = %ShareObject(key);
  externalizeString(shared_key);
  %InternalizeString("25963");
  %InternalizeString(shared_key);
  o[shared_key] = 'foo';
  assertTrue(Reflect.has(o, shared_key));
}

CheckInternalizedExternal();
