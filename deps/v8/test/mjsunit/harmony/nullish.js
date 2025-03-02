// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Basic sanity checks.
assertTrue(true ?? false);
assertFalse(false ?? true);
assertTrue(undefined ?? true);
assertTrue(null ?? true);
assertEquals([], [] ?? true);

// Chaining nullish.
assertTrue(null ?? null ?? true);
assertTrue(null ?? null ?? true ?? null);
assertTrue(undefined ?? undefined ?? true);
assertTrue(undefined ?? undefined ?? true ?? undefined);
assertFalse(null ?? false ?? null);
assertFalse(undefined ?? false ?? undefined);

// Nullish and conditionals.
assertTrue(null ?? true ? true : false);
assertTrue(null ?? null ?? true ? true : false);
assertTrue(undefined ?? true ? true : false);
assertTrue(undefined ?? undefined ?? true ? true : false);

// Nullish mixed expressions.
assertTrue(null ?? 1 == 1);
assertTrue(undefined ?? 1 == 1);
assertTrue(null ?? null ?? 1 == 1);
assertTrue(undefined ??undefined ?? 1 == 1);
assertEquals(1, null ?? 1 | 0);
assertEquals(1, undefined ?? 1 | 0);
assertEquals(1, null ?? null ?? 1 | 0);
assertEquals(1, undefined ?? undefined ?? 1 | 0);

// Short circuit.
{
  let ran = false;
  let never_ran = () => { ran = true; }
  let value = true ?? never_ran();
  assertTrue(value);
  assertFalse(ran);
}

{
  let ran = false;
  let never_ran = () => { ran = true; }
  let value = undefined ?? true ?? never_ran();
  assertTrue(value);
  assertFalse(ran);
}

{
  let ran = false;
  let never_ran = () => { ran = true; }
  let value = null ?? true ?? never_ran();
  assertTrue(value);
  assertFalse(ran);
}

// Nullish in tests evaluates only once.
{
  let run_count = 0;
  let run = () => { run_count++; return null; }
  if (run() ?? true) {} else { assertUnreachable(); }
  assertEquals(1, run_count);
}

// Nullish may not contain or be contained within || or &&.
assertThrows("true || true ?? true", SyntaxError);
assertThrows("true ?? true || true", SyntaxError);
assertThrows("true && true ?? true", SyntaxError);
assertThrows("true ?? true && true", SyntaxError);

// Test boolean expressions and nullish.
assertTrue((false || true) ?? false);
assertTrue(null ?? (false || true));
assertTrue((false || null) ?? true);
assertTrue((false || null) ?? (true && null) ?? true);
assertTrue((false || undefined) ?? true);
assertTrue((false || undefined) ?? (true && undefined) ?? true);
assertTrue(null ?? (false || true));
assertTrue(undefined ?? (false || true));
assertTrue(null ?? (false || null) ?? true);
assertTrue(undefined ?? (false || undefined) ?? true);
assertTrue(null ?? null ?? (false || true));
assertTrue(undefined ?? undefined ?? (false || true));
assertTrue((undefined ?? false) || true);
assertTrue((null ?? false) || true);
assertTrue((undefined ?? undefined ?? false) || false || true);
assertTrue((null ?? null ?? false) || false || true);
assertTrue(false || (undefined ?? true));
assertTrue(false || (null ?? true));
assertTrue(false || false || (undefined ?? undefined ?? true));
assertTrue(false || false || (null ?? null ?? true));

// Test use when test true.
if (undefined ?? true) {} else { assertUnreachable(); }
if (null ?? true) {} else { assertUnreachable(); }

if (undefined ?? undefined ?? true) {} else { assertUnreachable(); }
if (null ?? null ?? true) {} else { assertUnreachable(); }

// test use when test false
if (undefined ?? false) { assertUnreachable(); } else {}
if (null ?? false) { assertUnreachable(); } else {}

if (undefined ?? undefined ?? false) { assertUnreachable(); } else {}
if (null ?? null ?? false) { assertUnreachable(); } else {}

if (undefined ?? false ?? true) { assertUnreachable(); } else {}
if (null ?? false ?? true) { assertUnreachable(); } else {}

// Test use with nested boolean.
if ((false || undefined) ?? true) {} else { assertUnreachable(); }
if ((false || null) ?? true) {} else { assertUnreachable(); }

if ((false || undefined) ?? undefined ?? true) {} else { assertUnreachable(); }
if ((false || null) ?? null ?? true) {} else { assertUnreachable(); }

if (undefined ?? (false || true)) {} else { assertUnreachable(); }
if (null ?? (false || true)) {} else { assertUnreachable(); }

if (undefined ?? undefined ?? (false || true)) {} else { assertUnreachable(); }
if (null ?? null ?? (false || true)) {} else { assertUnreachable(); }

if (undefined ?? (false || undefined) ?? true) {} else { assertUnreachable(); }
if (null ?? (false || null) ?? true) {} else { assertUnreachable(); }

// Nested nullish.
if ((null ?? true) || false) {} else { assertUnreachable(); }
if ((null ?? null ?? true) || false) {} else { assertUnreachable(); }

if (false || (null ?? true)) {} else { assertUnreachable(); }
if (false || (null ?? null ?? true)) {} else { assertUnreachable(); }

if ((null ?? false) || false) { assertUnreachable(); } else {}
if ((null ?? null ?? false) || false) { assertUnreachable(); } else {}

if (false || (null ?? false)) { assertUnreachable(); } else {}
if (false || (null ?? null ?? false)) { assertUnreachable(); } else {}
