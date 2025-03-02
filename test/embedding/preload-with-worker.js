// Print the globalThis.preloadValue set by the preload script.
const mainPreloadValue = globalThis.preloadValue;

// Test that the preload script is executed in the worker thread.
const { Worker } = require('worker_threads');
const worker = new Worker(
  'require("worker_threads").parentPort.postMessage({value: globalThis.preloadValue})',
  { eval: true }
);

const messages = [];
worker.on('message', (message) => messages.push(message));

process.on('beforeExit', () => {
  console.log(
    `preloadValue=${mainPreloadValue}; worker preloadValue=${messages[0].value}`
  );
});
