// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --

(function NonExtensibleBetweenSetterAndGetter() {
  o = {};
  o.x = 42;
  o.__defineGetter__('y', function() {});
  Object.preventExtensions(o);
  o.__defineSetter__('y', function() {});
  o.x = 0.1;
})();

(function InterleavedIntegrityLevel() {
  o = {};
  o.x = 42;
  o.__defineSetter__('y', function() {});
  Object.preventExtensions(o);
  o.__defineGetter__('y', function() {
    return 44;
  });
  Object.seal(o);
  o.x = 0.1;
  assertEquals(44, o.y);
})();

(function TryUpdateRepeatedIntegrityLevel() {
  function C() {
    this.x = 0;
    this.x = 1;
    Object.preventExtensions(this);
    Object.seal(this);
  }

  const o1 = new C();
  const o2 = new C();
  const o3 = new C();

  function f(o) {
    return o.x;
  }

  // Warm up the IC.
  %PrepareFunctionForOptimization(f);
  f(o1);
  f(o1);
  f(o1);

  // Reconfigure to double field.
  o3.x = 0.1;

  // Migrate o2 to the new shape. (This won't mark the new map as a migration target.)
  assertEquals(o2, %TryMigrateInstance(o2));

  %OptimizeFunctionOnNextCall(f);
  // Calling the function with the deprecated o1 should still be ok.
  f(o1);

  // Ensure o1 is migrated (e.g. in jitless mode, where the unoptimized f won't
  // necessarily migrate it).

  let migrated = %TryMigrateInstance(o1);
  if (typeof migrated === 'number') {
    // No migration happened (o1 was already migrated).
    migrated = o1;
  }

  assertEquals(o1, migrated);

  assertTrue(%HaveSameMap(o1, o2));
  assertTrue(%HaveSameMap(o1, o3));
})();
