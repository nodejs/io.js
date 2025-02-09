'use strict';
const assert = require('assert');
let GCProfiler;

function collectGCProfile({ duration }) {
  GCProfiler ??= require('v8').GCProfiler;
  const { promise, resolve } = Promise.withResolvers();
  const profiler = new GCProfiler();
  profiler.start();
  setTimeout(() => {
    resolve(profiler.stop());
  }, duration);
  return promise;
}

function checkGCProfile(data) {
  assert.ok(data.version > 0);
  assert.ok(data.startTime >= 0);
  assert.ok(data.endTime >= 0);
  assert.ok(Array.isArray(data.statistics));
  // If the array is not empty, check it
  if (data.statistics.length) {
    // Just check the first one
    const item = data.statistics[0];
    assert.ok(typeof item.gcType === 'string');
    assert.ok(item.cost >= 0);
    assert.ok(typeof item.beforeGC === 'object');
    assert.ok(typeof item.afterGC === 'object');
    // The content of beforeGC and afterGC is same, so we just check afterGC
    assert.ok(typeof item.afterGC.heapStatistics === 'object');
    const heapStatisticsKeys = [
      'externalMemory',
      'heapSizeLimit',
      'mallocedMemory',
      'peakMallocedMemory',
      'totalAvailableSize',
      'totalGlobalHandlesSize',
      'totalHeapSize',
      'totalHeapSizeExecutable',
      'totalPhysicalSize',
      'usedGlobalHandlesSize',
      'usedHeapSize',
    ];
    heapStatisticsKeys.forEach((key) => {
      assert.ok(item.afterGC.heapStatistics[key] >= 0);
    });
    assert.ok(typeof item.afterGC.heapSpaceStatistics === 'object');
    const heapSpaceStatisticsKeys = [
      'physicalSpaceSize',
      'spaceAvailableSize',
      'spaceName',
      'spaceSize',
      'spaceUsedSize',
    ];
    heapSpaceStatisticsKeys.forEach((key) => {
      const value = item.afterGC.heapSpaceStatistics[0][key];
      assert.ok(key === 'spaceName' ? typeof value === 'string' : value >= 0);
    });
  }
}

async function testGCProfiler() {
  const data = await collectGCProfile({ duration: 5000 });
  checkGCProfile(data);
}

module.exports = {
  collectGCProfile,
  checkGCProfile,
  testGCProfiler,
};
