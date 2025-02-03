'use strict';
// Flags: --expose-gc --no-concurrent-array-buffer-sweeping
require('../common');
const assert = require('assert');
const zlib = require('zlib');

// Tests that native zlib handles start out their life as weak handles.

globalThis.gc();
const before = process.memoryUsage().external;
for (let i = 0; i < 100; ++i)
  zlib.createGzip();
const afterCreation = process.memoryUsage().external;
globalThis.gc();
const afterGC = process.memoryUsage().external;

assert((afterGC - before) / (afterCreation - before) <= 0.05,
       `Expected after-GC delta ${afterGC - before} to be less than 5 %` +
       ` of before-GC delta ${afterCreation - before}`);
