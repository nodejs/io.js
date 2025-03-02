// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --sandbox-testing --expose-gc

function workerFunc() {
  let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));
  let iteration = 0;
  let address;
  let valueA = 0;
  let valueB = 255;
  onmessage = function(e) {
    address = e.data.addr;
    setTimeout(doWork);
  }
  function doWork() {
    if (iteration == 0) postMessage({running: true});
    for (let i = 0; i < 21; i++) {
      iteration++;
      let value = iteration % 2 == 0 ? valueA : valueB;
      memory.setUint8(address, value);
    }
    setTimeout(doWork);
  }
}
let memory_corruption_worker = new Worker(workerFunc, {type: 'function'});

let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));
function DigitCount(bigint) {
  let addr = Sandbox.getAddressOf(bigint);
  return memory.getUint32(addr + 4, true) >> 1;
}

// Matches ToStringFormatter's "chunk_divisor_".
let divisor = 10n ** 19n;

// For future reference, these are interesting values that trigger different
// specific ASan reports:
//let kTargetDigits = 127;
//let kTargetDigits = 505;
//let kTargetDigits = 1010;
//let kTargetDigits = 2020;
let kTargetDigits = 4040;

// Find one of the divisors that the to-string algorithm would use
// internally, then construct a BigInt that has the same number of
// 64-bit digits, but only sets the lowest bit in its topmost digit.
// The worker will then keep toggling the highest bits in that digit.
while (DigitCount(divisor) < kTargetDigits) divisor *= divisor;
let digits = DigitCount(divisor);
let bits = (digits - 1) * 64;
let bigint = 1n << BigInt(bits);
if (DigitCount(bigint) !== digits) throw new Error("digit count is off");

// Promote the BigInt so it won't move later.
gc(); gc();

let addr = Sandbox.getAddressOf(bigint);
// *8: bytes per digit
// +8: BigInt header size
// -1: get the last in-bounds byte
let top_byte_offset = digits * 8 + 8 - 1;

memory_corruption_worker.onmessage = function(e) {
  // Run a couple of times to give the worker a chance to win the race.
  for (let i = 0; i < 20; i++) {
    console.log(("" + bigint).length);
  }
  quit();
}
memory_corruption_worker.postMessage({ addr: addr + top_byte_offset });
