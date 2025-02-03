// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var sab = new SharedArrayBuffer(4);
var sta = new Int8Array(sab);
sta[0] = 5;
function workerCode() {
  onmessage = function({data:msg}) {
    postMessage(0);
  };
}
var worker = new Worker(workerCode, {type: 'function'});

var value_obj = {
  valueOf: function() {worker.postMessage({sab:sab}, [sta.buffer]);
                       return 5}
}
var value = Object.create(value_obj);

assertThrows(function() {Atomics.exchange(sta, 0, value)});
assertThrows(function() {Atomics.compareExchange(sta, 0, 5, value)});
assertThrows(function() {Atomics.compareExchange(sta, 0, value, 5)});
assertThrows(function() {Atomics.add(sta, 0, value)});
assertThrows(function() {Atomics.sub(sta, 0, value)});
assertThrows(function() {Atomics.and(sta, 0, value)});
assertThrows(function() {Atomics.or(sta, 0, value)});
assertThrows(function() {Atomics.xor(sta, 0, value)});

var index_obj = {
  valueOf: function() {worker.postMessage({sab:sab}, [sta.buffer]);
                       return 0}
}
var index = Object.create(index_obj);

assertThrows(function() {Atomics.exchange(sta, index, 1)});
assertThrows(function() {Atomics.compareExchange(sta, index, 5, 2)});
assertThrows(function() {Atomics.add(sta, index, 3)});
assertThrows(function() {Atomics.sub(sta, index, 4)});
assertThrows(function() {Atomics.and(sta, index, 5)});
assertThrows(function() {Atomics.or(sta, index, 6)});
assertThrows(function() {Atomics.xor(sta, index, 7)});
