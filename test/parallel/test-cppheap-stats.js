'use strict';
// Tests that v8.getCppHeapStatistics() returns an object with an expected shape.

require('../common');
const assert = require('assert');
const v8 = require('v8');

// Detailed heap statistics
const heapStatsDetailed = v8.getCppHeapStatistics('detailed');
assert.strictEqual(heapStatsDetailed.detail_level, 'detailed');
const expectedTopLevelKeys = [
  'committed_size_bytes',
  'resident_size_bytes',
  'used_size_bytes',
  'detail_level',
  'space_statistics',
  'type_names',
].sort();

// Check top level properties
const actualTopLevelKeys = Object.keys(heapStatsDetailed).sort();
assert.deepStrictEqual(actualTopLevelKeys, expectedTopLevelKeys);

// Check types of top level properties
assert.strictEqual(typeof heapStatsDetailed.committed_size_bytes, 'number');
assert.strictEqual(typeof heapStatsDetailed.resident_size_bytes, 'number');
assert.strictEqual(typeof heapStatsDetailed.used_size_bytes, 'number');
assert.strictEqual(typeof heapStatsDetailed.detail_level, 'string');
assert.strictEqual(Array.isArray(heapStatsDetailed.space_statistics), true);
assert.strictEqual(Array.isArray(heapStatsDetailed.type_names), true);


// Check space statistics array
const expectedSpaceKeys = [
  'name',
  'committed_size_bytes',
  'resident_size_bytes',
  'used_size_bytes',
  'page_stats',
  'free_list_stats',
].sort();

heapStatsDetailed.space_statistics.forEach((space) => {
  const actualSpaceKeys = Object.keys(space).sort();
  assert.deepStrictEqual(actualSpaceKeys, expectedSpaceKeys);
  assert.strictEqual(typeof space.name, 'string');
  assert.strictEqual(typeof space.committed_size_bytes, 'number');
  assert.strictEqual(typeof space.resident_size_bytes, 'number');
  assert.strictEqual(typeof space.used_size_bytes, 'number');
  assert.strictEqual(Array.isArray(space.page_stats), true);
  assert.strictEqual(typeof space.free_list_stats, 'object');
});

// Check page statistics array
const expectedPageKeys = [
  'committed_size_bytes',
  'resident_size_bytes',
  'used_size_bytes',
  'object_statistics',
].sort();

heapStatsDetailed.space_statistics.forEach((space) => {
  space.page_stats.forEach((page) => {
    const actualPageKeys = Object.keys(page).sort();
    assert.deepStrictEqual(actualPageKeys, expectedPageKeys);
    assert.strictEqual(typeof page.committed_size_bytes, 'number');
    assert.strictEqual(typeof page.resident_size_bytes, 'number');
    assert.strictEqual(typeof page.used_size_bytes, 'number');
    assert.strictEqual(Array.isArray(page.object_statistics), true);
  });
});

// Check free list statistics
const expectedFreeListKeys = ['bucket_size', 'free_count', 'free_size'].sort();

heapStatsDetailed.space_statistics.forEach((space) => {
  const actualFreeListKeys = Object.keys(space.free_list_stats).sort();
  assert.deepStrictEqual(actualFreeListKeys, expectedFreeListKeys);
  assert.strictEqual(Array.isArray(space.free_list_stats.bucket_size), true);
  assert.strictEqual(Array.isArray(space.free_list_stats.free_count), true);
  assert.strictEqual(Array.isArray(space.free_list_stats.free_size), true);
});

// Check object statistics
const expectedObjectStatsKeys = ['allocated_bytes', 'object_count'].sort();

heapStatsDetailed.space_statistics.forEach((space) => {
  space.page_stats.forEach((page) => {
    page.object_statistics.forEach((objectStats) => {
      const actualObjectStatsKeys = Object.keys(objectStats).sort();
      assert.deepStrictEqual(actualObjectStatsKeys, expectedObjectStatsKeys);
      assert.strictEqual(typeof objectStats.allocated_bytes, 'number');
      assert.strictEqual(typeof objectStats.object_count, 'number');
    });
  });
});

// Check type names
heapStatsDetailed.type_names.forEach((typeName) => {
  assert.strictEqual(typeof typeName, 'string');
});

// Brief heap statistics
const heapStatsBrief = v8.getCppHeapStatistics('brief');
const expectedBriefKeys = [
  'committed_size_bytes',
  'resident_size_bytes',
  'used_size_bytes',
  'detail_level',
  'space_statistics',
  'type_names',
].sort();

// Check top level properties
const actualBriefKeys = Object.keys(heapStatsBrief).sort();
assert.strictEqual(heapStatsBrief.detail_level, 'brief');
assert.deepStrictEqual(actualBriefKeys, expectedBriefKeys);

// Check types of top level properties
assert.strictEqual(typeof heapStatsBrief.committed_size_bytes, 'number');
assert.strictEqual(typeof heapStatsBrief.resident_size_bytes, 'number');
assert.strictEqual(typeof heapStatsBrief.used_size_bytes, 'number');
assert.strictEqual(typeof heapStatsBrief.detail_level, 'string');
assert.strictEqual(Array.isArray(heapStatsBrief.space_statistics), true);
assert.strictEqual(Array.isArray(heapStatsBrief.type_names), true);
