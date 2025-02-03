'use strict';

// Flags: --expose-gc

const common = require('../../common');
const { gcUntil } = require('../../common/gc');
// Verify that addons can create GarbageCollected objects and
// have them traced properly.

const assert = require('assert');
const {
  CppGCed, states, kDestructCount, kTraceCount,
} = require(`./build/${common.buildType}/binding`);

assert.strictEqual(states[kDestructCount], 0);
assert.strictEqual(states[kTraceCount], 0);

let array = [];
const count = 100;
for (let i = 0; i < count; ++i) {
  array.push(new CppGCed());
}

globalThis.gc();

setTimeout(async function() {
  // GC should have invoked Trace() on at least some of the CppGCed objects,
  // but they should all be alive at this point.
  assert.strictEqual(states[kDestructCount], 0);
  assert.notStrictEqual(states[kTraceCount], 0);

  // Replace the old CppGCed objects with new ones, after GC we should have
  // destructed all the old ones and called Trace() on the
  // new ones.
  for (let i = 0; i < count; ++i) {
    array[i] = new CppGCed();
  }
  await gcUntil(
    'All old CppGCed are destroyed',
    () => states[kDestructCount] === count,
  );
  // Release all the CppGCed objects, after GC we should have destructed
  // all of them.
  array = null;
  globalThis.gc();

  await gcUntil(
    'All old CppGCed are destroyed',
    () => states[kDestructCount] === count * 2,
  );
}, 1);
