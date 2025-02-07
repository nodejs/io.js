'use strict';

const wait = require('timers/promises').setTimeout;
const assert = require('assert');
const common = require('../common');
// TODO(joyeecheung): rewrite checkIfCollectable to use this too.
const { setImmediate: setImmediatePromisified } = require('timers/promises');
const gcTrackerMap = new WeakMap();
const gcTrackerTag = 'NODE_TEST_COMMON_GC_TRACKER';

/**
 * Installs a garbage collection listener for the specified object.
 * Uses async_hooks for GC tracking, which may affect test functionality.
 * A full setImmediate() invocation passes between a global.gc() call and the listener being invoked.
 * @param {object} obj - The target object to track for garbage collection.
 * @param {object} gcListener - The listener object containing the ongc callback.
 * @param {Function} gcListener.ongc - The function to call when the target object is garbage collected.
 */
function onGC(obj, gcListener) {
  const async_hooks = require('async_hooks');

  const onGcAsyncHook = async_hooks.createHook({
    init: common.mustCallAtLeast(function(id, type) {
      if (this.trackedId === undefined) {
        assert.strictEqual(type, gcTrackerTag);
        this.trackedId = id;
      }
    }),
    destroy(id) {
      assert.notStrictEqual(this.trackedId, -1);
      if (id === this.trackedId) {
        this.gcListener.ongc();
        onGcAsyncHook.disable();
      }
    },
  }).enable();
  onGcAsyncHook.gcListener = gcListener;

  gcTrackerMap.set(obj, new async_hooks.AsyncResource(gcTrackerTag));
  obj = null;
}

/**
 * Repeatedly triggers garbage collection until a specified condition is met or a maximum number of attempts is reached.
 * This utillity must be run in a Node.js instance that enables --expose-gc.
 * @param {string|Function} [name] - Optional name, used in the rejection message if the condition is not met.
 * @param {Function} condition - A function that returns true when the desired condition is met.
 * @param {number} maxCount - Maximum number of garbage collections that should be tried.
 * @param {object} gcOptions - Options to pass into the global gc() function.
 * @returns {Promise} A promise that resolves when the condition is met, or rejects after 10 failed attempts.
 */
async function gcUntil(name, condition, maxCount = 10, gcOptions) {
  for (let count = 0; count < maxCount; ++count) {
    await setImmediatePromisified();
    if (gcOptions) {
      await global.gc(gcOptions);
    } else {
      await global.gc();  // Passing in undefined is not the same as empty.
    }
    if (condition()) {
      return;
    }
  }
  throw new Error(`Test ${name} failed`);
}

// This function can be used to check if an object factor leaks or not,
// but it needs to be used with care:
// 1. The test should be set up with an ideally small
//   --max-old-space-size or --max-heap-size, which combined with
//   the maxCount parameter can reproduce a leak of the objects
//   created by fn().
// 2. This works under the assumption that if *none* of the objects
//   created by fn() can be garbage-collected, the test would crash due
//   to OOM.
// 3. If *any* of the objects created by fn() can be garbage-collected,
//   it is considered leak-free. The FinalizationRegistry is used to
//   terminate the test early once we detect any of the object is
//   garbage-collected to make the test less prone to false positives.
//   This may be especially important for memory management relying on
//   emphemeron GC which can be inefficient to deal with extremely fast
//   heap growth.
// Note that this can still produce false positives. When the test using
// this function still crashes due to OOM, inspect the heap to confirm
// if a leak is present (e.g. using heap snapshots).
// The generateSnapshotAt parameter can be used to specify a count
// interval to create the heap snapshot which may enforce a more thorough GC.
// This can be tried for code paths that require it for the GC to catch up
// with heap growth. However this type of forced GC can be in conflict with
// other logic in V8 such as bytecode aging, and it can slow down the test
// significantly, so it should be used scarcely and only as a last resort.
async function checkIfCollectable(
  fn, maxCount = 4096, generateSnapshotAt = Infinity, logEvery = 128) {
  let anyFinalized = false;
  let count = 0;

  const f = new FinalizationRegistry(() => {
    anyFinalized = true;
  });

  async function createObject() {
    const obj = await fn();
    f.register(obj);
    if (count++ < maxCount && !anyFinalized) {
      setImmediate(createObject, 1);
    }
    // This can force a more thorough GC, but can slow the test down
    // significantly in a big heap. Use it with care.
    if (count % generateSnapshotAt === 0) {
      // XXX(joyeecheung): This itself can consume a bit of JS heap memory,
      // but the other alternative writeHeapSnapshot can run into disk space
      // not enough problems in the CI & be slower depending on file system.
      // Just do this for now as long as it works and only invent some
      // internal voodoo when we absolutely have no other choice.
      require('v8').getHeapSnapshot().pause().read();
      console.log(`Generated heap snapshot at ${count}`);
    }
    if (count % logEvery === 0) {
      console.log(`Created ${count} objects`);
    }
    if (anyFinalized) {
      console.log(`Found finalized object at ${count}, stop testing`);
    }
  }

  createObject();
}

// Repeat an operation and give GC some breathing room at every iteration.
async function runAndBreathe(fn, repeat, waitTime = 20) {
  for (let i = 0; i < repeat; i++) {
    await fn();
    await wait(waitTime);
  }
}

/**
 * This requires --expose-internals.
 * This function can be used to check if an object factory leaks or not by
 * iterating over the heap and count objects with the specified class
 * (which is checked by looking up the prototype chain).
 * @param {(i: number) => number} fn The factory receiving iteration count
 *   and returning number of objects created. The return value should be
 *   precise otherwise false negatives can be produced.
 * @param {Function} ctor The constructor of the objects being counted.
 * @param {number} count Number of iterations that this check should be done
 * @param {number} waitTime Optional breathing time for GC.
 */
async function checkIfCollectableByCounting(fn, ctor, count, waitTime = 20) {
  const { queryObjects } = require('v8');
  const { name } = ctor;
  const initialCount = queryObjects(ctor, { format: 'count' });
  console.log(`Initial count of ${name}: ${initialCount}`);
  let totalCreated = 0;
  for (let i = 0; i < count; ++i) {
    const created = await fn(i);
    totalCreated += created;
    console.log(`#${i}: created ${created} ${name}, total ${totalCreated}`);
    await wait(waitTime);  // give GC some breathing room.
    const currentCount = queryObjects(ctor, { format: 'count' });
    const collected = totalCreated - (currentCount - initialCount);
    console.log(`#${i}: counted ${currentCount} ${name}, collected ${collected}`);
    if (collected > 0) {
      console.log(`Detected ${collected} collected ${name}, finish early`);
      return;
    }
  }

  await wait(waitTime);  // give GC some breathing room.
  const currentCount = queryObjects(ctor, { format: 'count' });
  const collected = totalCreated - (currentCount - initialCount);
  console.log(`Last count: counted ${currentCount} ${name}, collected ${collected}`);
  // Some objects with the prototype can be collected.
  if (collected > 0) {
    console.log(`Detected ${collected} collected ${name}`);
    return;
  }

  throw new Error(`${name} cannot be collected`);
}

module.exports = {
  checkIfCollectable,
  runAndBreathe,
  checkIfCollectableByCounting,
  onGC,
  gcUntil,
};
