// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import foreign, { url as otherUrl } from './modules-skip-export-import-meta.mjs';

assertEquals("object", typeof import.meta);
assertEquals(null, Object.getPrototypeOf(import.meta));
assertSame(import.meta, import.meta);

const loadImportMetaArrow = () => import.meta;
assertSame(loadImportMetaArrow(), import.meta);
function loadImportMetaFn() {
  try {
    throw new Error('force catch code path for nested context');
  } catch (e) {
    return import.meta;
  }
}
loadImportMetaFn();
assertSame(loadImportMetaFn(), import.meta);

// This property isn't part of the spec itself but is mentioned as an example
assertMatches(/\/modules-import-meta\.mjs$/, import.meta.url);

import.meta.x = 42;
assertEquals(42, import.meta.x);
Object.assign(import.meta, { foo: "bar" })
assertEquals("bar", import.meta.foo);

// PerformEval parses its argument for the goal symbol Script. So the following
// should fail just as it does for every other Script context.
//
// See:
// https://github.com/tc39/proposal-import-meta/issues/7#issuecomment-329363083
assertThrows(() => eval('import.meta'), SyntaxError);
assertThrows(() => new Function('return import.meta;'), SyntaxError);

assertNotEquals(foreign, import.meta);
assertMatches(/\/modules-skip-export-import-meta\.mjs$/, foreign.url);
assertEquals(foreign.url, otherUrl);
