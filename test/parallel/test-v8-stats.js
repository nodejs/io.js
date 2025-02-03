'use strict';
require('../common');
const assert = require('assert');
const v8 = require('v8');

const s = v8.getHeapStatistics();
const keys = [
  'does_zap_garbage',
  'external_memory',
  'heap_size_limit',
  'malloced_memory',
  'number_of_detached_contexts',
  'number_of_native_contexts',
  'peak_malloced_memory',
  'total_available_size',
  'total_global_handles_size',
  'total_heap_size',
  'total_heap_size_executable',
  'total_physical_size',
  'used_global_handles_size',
  'used_heap_size'];
assert.deepStrictEqual(Object.keys(s).sort(), keys);
keys.forEach(function(key) {
  assert.strictEqual(typeof s[key], 'number');
});


const heapCodeStatistics = v8.getHeapCodeStatistics();
const heapCodeStatisticsKeys = [
  'bytecode_and_metadata_size',
  'code_and_metadata_size',
  'cpu_profiler_metadata_size',
  'external_script_source_size'];
assert.deepStrictEqual(Object.keys(heapCodeStatistics).sort(),
                       heapCodeStatisticsKeys);
heapCodeStatisticsKeys.forEach(function(key) {
  assert.strictEqual(typeof heapCodeStatistics[key], 'number');
});


const expectedHeapSpaces = [
  'code_large_object_space',
  'code_space',
  'large_object_space',
  'new_large_object_space',
  'new_space',
  'old_space',
  'read_only_space',
  'shared_large_object_space',
  'shared_space',
  'shared_trusted_large_object_space',
  'shared_trusted_space',
  'trusted_large_object_space',
  'trusted_space',
];
const heapSpaceStatistics = v8.getHeapSpaceStatistics();
const actualHeapSpaceNames = heapSpaceStatistics.map((s) => s.space_name);
assert.deepStrictEqual(actualHeapSpaceNames.sort(), expectedHeapSpaces.sort());
heapSpaceStatistics.forEach((heapSpace) => {
  assert.strictEqual(typeof heapSpace.space_name, 'string');
  assert.strictEqual(typeof heapSpace.space_size, 'number');
  assert.strictEqual(typeof heapSpace.space_used_size, 'number');
  assert.strictEqual(typeof heapSpace.space_available_size, 'number');
  assert.strictEqual(typeof heapSpace.physical_space_size, 'number');
});
