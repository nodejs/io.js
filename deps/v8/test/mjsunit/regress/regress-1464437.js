// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function get_keys() {
  const buffer = new ArrayBuffer(12, { "maxByteLength": 4096 });
  const u16array = new Uint16Array(buffer, 0, 5);
  buffer.resize()
  let keys = "none";
  try { keys = u16array.keys(); } catch (e) {}
  return keys;
}

(function() {
  // Testing array.prototype.keys
  %PrepareFunctionForOptimization(get_keys);
  let interpreted = get_keys();
  assertEquals("none", interpreted);
  %OptimizeFunctionOnNextCall(get_keys);
  let optimized = get_keys();
  assertEquals("none", optimized);
})();

function get_keys_tracking() {
  const buffer = new ArrayBuffer(12, { "maxByteLength": 4096 });
  const u16array = new Uint16Array(buffer,  6);
  buffer.resize(0);
  let keys = "none";
  try { keys = u16array.keys(); } catch (e) {}
  return keys;
}

(function() {
 // Testing array.prototype.keys
  %PrepareFunctionForOptimization(get_keys_tracking);
  let interpreted = get_keys_tracking();
  assertEquals("none", interpreted);
  %OptimizeFunctionOnNextCall(get_keys_tracking);
  let optimized = get_keys_tracking();
  assertEquals("none", optimized);
})();

function get_values() {
  const buffer = new ArrayBuffer(12, { "maxByteLength": 4096 });
  const u16array = new Uint16Array(buffer, 0, 5);
  buffer.resize()
  let keys = "none";
  try { keys = u16array.values(); } catch (e) {}
  return keys;
}

(function() {
  // Testing array.prototype.values
  %PrepareFunctionForOptimization(get_values);
  let interpreted = get_values();
  assertEquals("none", interpreted);
  %OptimizeFunctionOnNextCall(get_values);
  let optimized = get_values();
  assertEquals("none", optimized);
})();

function get_entries() {
  const buffer = new ArrayBuffer(12, { "maxByteLength": 4096 });
  const u16array = new Uint16Array(buffer, 0, 5);
  buffer.resize()
  let keys = "none";
  try { keys = u16array.entries(); } catch (e) {}
  return keys;
}

(function() {
  // Testing array.prototype.entries
  %PrepareFunctionForOptimization(get_entries);
  let interpreted = get_entries();
  assertEquals("none", interpreted);
  %OptimizeFunctionOnNextCall(get_entries);
  let optimized = get_entries();
  assertEquals("none", optimized);
})();
