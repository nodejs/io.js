// Check that the linked modules defined in the executable can be used in
// the main thread and worker thread.

// Use the greeter and replicator modules in the main thread.
const greeterModule = process._linkedBinding('greeter_module');
const replicatorModule = process._linkedBinding('replicator_module');

const names = replicatorModule.replicate('World');
const greeting = greeterModule.greet(names);

// Use the greeter and replicator modules in the worker thread.
const { Worker } = require('worker_threads');
const worker = new Worker(
  'const greeterModule = process._linkedBinding("greeter_module");\n' +
    'const replicatorModule = process._linkedBinding("replicator_module");\n' +
    'const names = replicatorModule.replicate("Node");\n' +
    'const greeting = greeterModule.greet(names);\n' +
    'require("worker_threads").parentPort.postMessage({greeting});',
  { eval: true }
);

const messages = [];
worker.on('message', (message) => messages.push(message));

// Print the greetings from the main thread and worker thread.
process.on('beforeExit', () => {
  console.log(`main=${greeting}; worker=${messages[0].greeting}`);
});
