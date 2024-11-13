'use strict';
const common = require('../common');
const assert = require('assert');
const { MessageChannel, Worker } = require('worker_threads');

// Test that SharedArrayBuffer instances created from WASM are transferable
// through MessageChannels (without crashing).

const fixtures = require('../common/fixtures');
const wasmSource = fixtures.readSync('shared-memory.wasm');
const wasmModule = new WebAssembly.Module(wasmSource);
const instance = new WebAssembly.Instance(wasmModule);

const { buffer } = instance.exports.memory;
assert(buffer instanceof SharedArrayBuffer);

{
  const { port1, port2 } = new MessageChannel();
  port1.postMessage(buffer);
  port2.once('message', common.mustCall((buffer2) => {
    // Make sure serialized + deserialized buffer refer to the same memory.
    const expected = 'Hello, world!';
    const bytes = Buffer.from(buffer).write(expected);
    const deserialized = Buffer.from(buffer2).toString('utf8', 0, bytes);
    assert.deepStrictEqual(deserialized, expected);
  }));
}

{
  // Make sure we can free WASM memory originating from a thread that already
  // stopped when we exit.
  const worker = new Worker(`
  const { parentPort } = require('worker_threads');

  // Compile the same WASM module from its source bytes.
  const wasmSource = new Uint8Array([${wasmSource.join(',')}]);
  const wasmModule = new WebAssembly.Module(wasmSource);
  const instance = new WebAssembly.Instance(wasmModule);
  parentPort.postMessage(instance.exports.memory);

  // Do the same thing, except we receive the WASM module via transfer.
  parentPort.once('message', ({ wasmModule }) => {
    const instance = new WebAssembly.Instance(wasmModule);
    parentPort.postMessage(instance.exports.memory);
  });
  `, { eval: true });
  worker.on('message', common.mustCall(({ buffer }) => {
    assert(buffer instanceof SharedArrayBuffer);
    worker.buf = buffer; // Basically just keep the reference to buffer alive.
  }, 2));
  worker.once('exit', common.mustCall());
  worker.postMessage({ wasmModule });
}
