// Enforcing strict checks on the order or number of events across different
// platforms can be tricky and unreliable due to various factors.
// As a result, this test relies on the `uv_metrics_info` call instead.
const { performance } = require('node:perf_hooks');
const assert = require('node:assert');
const fs = require('node:fs');
const { nodeTiming } = performance;

function safeMetricsInfo(cb) {
  setImmediate(() => {
    const info = nodeTiming.uvMetricsInfo;
    cb(info);
  });
}

{
  const info = nodeTiming.uvMetricsInfo;
  assert.strictEqual(info.loopCount, 0);
  assert.strictEqual(info.events, 0);
  // This is the only part of the test that we test events waiting
  // Adding checks for this property will make the test flaky
  // as it can be highly influenced by race conditions.
  assert.strictEqual(info.eventsWaiting, 0);
}

{
  // The synchronous call should obviously not affect the uv metrics
  const fd = fs.openSync(__filename, 'r');
  fs.readFileSync(fd);
  const info = nodeTiming.uvMetricsInfo;
  assert.strictEqual(info.loopCount, 0);
  assert.strictEqual(info.events, 0);
  assert.strictEqual(info.eventsWaiting, 0);
}

{
  function openFile(info) {
    assert.strictEqual(info.loopCount, 1);

    fs.open(__filename, 'r', (err) => {
      assert.ifError(err);
    });
  }

  safeMetricsInfo(openFile);
}