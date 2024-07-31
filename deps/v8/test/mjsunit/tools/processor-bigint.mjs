// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --logfile='+' --log --log-maps --log-ic --log-code
// Flags: --log-function-events --no-predictable

import { Processor } from "../../../tools/system-analyzer/processor.mjs";

// log code start
function doWork() {
  let array = [];
  for (let i = 0; i < 500; i++) {
    doWorkStep(i, array);
  }
  let sum = 0;
  for (let i = 0; i < 500; i++) {
    sum += array[i]["property" + i];
  }
  return sum;
}

function doWorkStep(i, array) {
  const obj = {
    ["property" + i]: i,
  };
  array.push(obj);
  obj.custom1 = 1;
  obj.custom2 = 2;
}

const result = doWork();
 // log code end

const logString = d8.log.getAndStop();
assertTrue(logString.length > 0);
const useBigIntAddresses = true;
const processor = new Processor(useBigIntAddresses);
await processor.processChunk(logString);
await processor.finalize();

const maps = processor.mapTimeline;
const ics = processor.icTimeline;
const scripts = processor.scripts;

(function testResults() {
  assertEquals(result, 124750);
  assertTrue(maps.length > 0);
  assertTrue(ics.length > 0);
  assertTrue(scripts.length > 0);
})();

(function testIcKeys() {
  const keys = new Set();
  ics.forEach(ic => keys.add(ic.key));
  assertTrue(keys.has("custom1"));
  assertTrue(keys.has("custom2"));
  assertTrue(keys.has("push"));
})();
