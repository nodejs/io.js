'use strict';

require('../common');
const { performance } = require('perf_hooks');
// Get the start time as soon as possible.
const testStartTime = performance.now();
const assert = require('assert');
const { writeSync } = require('fs');
const { isMainThread } = require('worker_threads');

// Use writeSync to stdout to avoid disturbing the loop.
function log(str) {
  writeSync(1, str + '\n');
}

assert(performance);
assert(performance.nodeTiming);
assert.strictEqual(typeof performance.timeOrigin, 'number');

assert(testStartTime > 0, `${testStartTime} <= 0`);
// Use a fairly large epsilon value, since we can only guarantee that the node
// process started up in 15 seconds.
assert(testStartTime < 15000, `${testStartTime} >= 15000`);

// Use different ways to calculate process uptime to check that
// performance.timeOrigin and performance.now() are in reasonable range.
const epsilon = 50;
{
  const uptime1 = Date.now() - performance.timeOrigin;
  const uptime2 = performance.now();
  const uptime3 = process.uptime() * 1000;
  assert(Math.abs(uptime1 - uptime2) < epsilon,
         `Date.now() - performance.timeOrigin (${uptime1}) - ` +
         `performance.now() (${uptime2}) = ` +
         `${uptime1 - uptime2} >= +- ${epsilon}`);
  assert(Math.abs(uptime1 - uptime3) < epsilon,
         `Date.now() - performance.timeOrigin (${uptime1}) - ` +
         `process.uptime() * 1000 (${uptime3}) = ` +
         `${uptime1 - uptime3} >= +- ${epsilon}`);
}

assert.strictEqual(performance.nodeTiming.name, 'node');
assert.strictEqual(performance.nodeTiming.entryType, 'node');

// If timing.duration gets copied into the argument instead of being computed
// via the getter, this should be called right after timing is created.
function checkNodeTiming(timing) {
  // Calculate the difference between now() and duration as soon as possible.
  const now = performance.now();
  const delta = Math.abs(now - timing.duration);

  log(JSON.stringify(timing, null, 2));
  // Check that the properties are still reasonable.
  assert.strictEqual(timing.name, 'node');
  assert.strictEqual(timing.entryType, 'node');

  // Check that duration is positive and practically the same as
  // performance.now() i.e. measures Node.js instance up time.
  assert.strictEqual(typeof timing.duration, 'number');
  assert(timing.duration > 0, `timing.duration ${timing.duration} <= 0`);
  assert(delta < 10,
         `now (${now}) - timing.duration (${timing.duration}) = ${delta} >= ${10}`);

  // Check that the following fields do not change.
  assert.strictEqual(timing.startTime, initialTiming.startTime);
  assert.strictEqual(timing.nodeStart, initialTiming.nodeStart);
  assert.strictEqual(timing.v8Start, initialTiming.v8Start);
  assert.strictEqual(timing.environment, initialTiming.environment);
  assert.strictEqual(timing.bootstrapComplete, initialTiming.bootstrapComplete);

  assert.strictEqual(typeof timing.loopStart, 'number');
  assert.strictEqual(typeof timing.loopExit, 'number');
}

log('check initial nodeTiming');
// Copy all the values from the getters.
const initialTiming = { ...performance.nodeTiming };
checkNodeTiming(initialTiming);

{
  const {
    startTime,
    nodeStart,
    v8Start,
    environment,
    bootstrapComplete,
  } = initialTiming;

  assert.strictEqual(startTime, 0);
  assert.strictEqual(typeof nodeStart, 'number');
  assert(nodeStart > 0, `nodeStart ${nodeStart} <= 0`);
  // The whole process starts before this test starts.
  assert(nodeStart < testStartTime,
         `nodeStart ${nodeStart} >= ${testStartTime}`);

  assert.strictEqual(typeof v8Start, 'number');
  assert(v8Start > 0, `v8Start ${v8Start} <= 0`);
  // V8 starts after the process starts.
  assert(v8Start > nodeStart, `v8Start ${v8Start} <= ${nodeStart}`);
  // V8 starts before this test starts.
  assert(v8Start < testStartTime,
         `v8Start ${v8Start} >= ${testStartTime}`);

  assert.strictEqual(typeof environment, 'number');
  assert(environment > 0, `environment ${environment} <= 0`);
  // Environment starts after V8 starts.
  assert(environment > v8Start,
         `environment ${environment} <= ${v8Start}`);
  // Environment starts before this test starts.
  assert(environment < testStartTime,
         `environment ${environment} >= ${testStartTime}`);

  assert.strictEqual(typeof bootstrapComplete, 'number');
  assert(bootstrapComplete > 0, `bootstrapComplete ${bootstrapComplete} <= 0`);
  // Bootstrap completes after environment starts.
  assert(bootstrapComplete > environment,
         `bootstrapComplete ${bootstrapComplete} <= ${environment}`);
  // Bootstrap completes before this test starts.
  assert(bootstrapComplete < testStartTime,
         `bootstrapComplete ${bootstrapComplete} >= ${testStartTime}`);
}

assert.strictEqual(initialTiming.loopExit, -1);

function checkValue(timing, name, min, max) {
  const value = timing[name];
  assert(value > 0, `${name} ${value} <= 0`);
  // Loop starts after bootstrap completes.
  assert(value > min,
         `${name} ${value} <= ${min}`);
  assert(value < max, `${name} ${value} >= ${max}`);
}

let loopStart = initialTiming.loopStart;
if (isMainThread) {
  // In the main thread, the loop does not start until we start an operation
  // that requires it, e.g. setTimeout().
  assert.strictEqual(initialTiming.loopStart, -1);
  log('Start timer');
  setTimeout(() => {
    log('Check nodeTiming in timer');
    const timing = { ...performance.nodeTiming };
    checkNodeTiming(timing);
    // Loop should start after we fire the timeout, and before we call
    // performance.now() here.
    loopStart = timing.loopStart;
    checkValue(timing, 'loopStart', initialTiming.duration, performance.now());
  }, 1000);
} else {
  // In the worker, the loop always starts before the user code is evaluated,
  // and after bootstrap completes.
  checkValue(initialTiming,
             'loopStart',
             initialTiming.bootstrapComplete,
             testStartTime);
}

process.on('exit', () => {
  log('Check nodeTiming in process exit event');
  const timing = { ...performance.nodeTiming };
  checkNodeTiming(timing);
  // Check that loopStart does not change.
  assert.strictEqual(timing.loopStart, loopStart);
  checkValue(timing,
             'loopExit',
             loopStart,
             performance.now());
});
