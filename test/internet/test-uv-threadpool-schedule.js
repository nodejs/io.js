'use strict';

// Test to validate massive dns lookups do not block filesystem I/O
// (or any fast I/O). Prior to https://github.com/libuv/libuv/pull/1845
// few back-to-back dns lookups were sufficient to engage libuv
// threadpool workers in a blocking manner, throttling other work items
// that need pool resources. Start slow and fast I/Os together, make sure
// fast I/O can complete in at least in 1/100th of time for slow I/O.
// TEST TIME TO COMPLETION: ~5 seconds.

const common = require('../common');
const dns = require('dns');
const fs = require('fs');
const assert = require('assert');

const start = Date.now();

const slowIOmax = 100;
let slowIOcount = 0;
let fastIOdone = false;
let slowIOend, fastIOend;

function onResolve() {
  slowIOcount++;
  if (slowIOcount === slowIOmax) {
    slowIOend = Date.now();

    // Conservative expectation: finish disc I/O
    // at least by when the net I/O completes.
    assert.ok(fastIOdone,
              'fast I/O was throttled due to threadpool congestion.');

    // More realistic expectation: finish disc I/O at least within
    // a time duration that is half of net I/O.
    // Ideally the slow I/O should not affect the fast I/O as those
    // have two different thread-pool buckets. However, this could be
    // highly load / platform dependent, so don't be very greedy.
    const fastIOtime = fastIOend - start;
    const slowIOtime = slowIOend - start;
    const expectedMax = slowIOtime / 2;
    assert.ok(fastIOtime < expectedMax,
              'fast I/O took longer to complete, ' +
              `actual: ${fastIOtime}, expected: ${expectedMax}`);
  }
}


for (let i = 0; i < slowIOmax; i++) {
  // We need to refresh the domain string everytime,
  // otherwise the TCP stack that cache the previous lookup
  // returns result from memory, breaking all our Math.
  dns.lookup(`${randomDomain()}.com`, {}, common.mustCall(onResolve));
}

fs.readFile(__filename, common.mustCall(() => {
  fastIOend = Date.now();
  fastIOdone = true;
}));

function randomDomain() {
  const d = Buffer.alloc(10);
  for (let i = 0; i < 10; i++)
    d[i] = 97 + (Math.round(Math.random() * 13247)) % 26;
  return d.toString();
}
