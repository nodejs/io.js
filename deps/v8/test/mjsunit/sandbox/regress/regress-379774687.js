// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --sandbox-testing --expose-gc

const kSeqStringType = Sandbox.getInstanceTypeIdFor("SEQ_TWO_BYTE_STRING_TYPE");
const kStringHashOffset = Sandbox.getFieldOffset(kSeqStringType, "hash");

let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

// Will return random (but deterministic, if a --random-seed is used) numbers.
function randomIntUpTo(n) {
  return Math.floor(Math.random() * n);
}

// Helper function that spawns a worker thread that corrupts memory in the
// background, constantly flipping the given address between two values.
function corruptInBackground(address, bitToFlip) {
  function workerTemplate(address, bitToFlip) {
    let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));
    let oldValue = memory.getUint32(address, true);
    let newValue = oldValue ^ bitToFlip;
    while (true) {
      memory.setUint32(address, newValue, true);
      memory.setUint32(address, oldValue, true);
    }
  }
  const workerCode = new Function(
      `(${workerTemplate})(${address}, ${bitToFlip})`);
  return new Worker(workerCode, {type: 'function'});
}

// Some random code for the parser...
// Need some unicode in here to get a Utf16 string (otherwise, we'd get a
// buffered stream during parsing, which probably complicates things a bit).
function test() {
  function log(msg) {}

  function launchRockets(n, destination) {
    class RocketLauncher {
      constructor(type) {
        this.rocketType = type ?? '🚀';
        this.launchCount = 0;
      }

      launch(destination) {
        log(`Launching ${this.rocketType} to ${destination}!`);
        this.launchCount++;
      }
    }

    if (typeof destination === 'undefined') {
      destination = "the moon";
    }

    let launcher = new RocketLauncher;
    for (let i = 0; i < n; i++) {
      launcher.launch(destination);
    }
  }

  launchRockets(3);
  launchRockets(2, "mars");
  launchRockets(1, "pluto");
}

// Create a SeqTwoByteString (otherwise we have a slice string)
let source = (test.toString() + "\ntest();").split('').join('');
assertEquals(Sandbox.getInstanceTypeIdOf(source), kSeqStringType);

// Trigger some GCs to move the object to a stable position in memory.
gc();
gc();

// Start the worker thread to corrupt the string in the background.
let size = Sandbox.getSizeOf(source);
assertTrue(size > source.length * 2);
let offset = randomIntUpTo(size);
let string_address = Sandbox.getAddressOf(source);
let bitToFlip = 1 << randomIntUpTo(32);
corruptInBackground(string_address + offset, bitToFlip);

for (let i = 0; i < 1000; i++) {
  // Modify the hash in between every attempt to avoid code caching.
  // Use + 0x4 here to not change the type bits (see HashFieldTypeBits).
  // Alternatively, use --no-compilation-cache.
  let currentHash = memory.getUint32(string_address + kStringHashOffset, true);
  let newHash = currentHash + 0x4;
  memory.setUint32(string_address + kStringHashOffset, newHash, true);

  try { eval(source); } catch {}
}
