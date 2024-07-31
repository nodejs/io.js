'use strict';

const {
  ObjectDefineProperty,
} = primordials;

delete process._debugProcess;
delete process._debugEnd;

function defineStream(name, getter) {
  ObjectDefineProperty(process, name, {
    __proto__: null,
    configurable: true,
    enumerable: true,
    get: getter,
  });
}

defineStream('stdout', getStdout);
defineStream('stdin', getStdin);
defineStream('stderr', getStderr);

// Worker threads don't receive signals.
const {
  startListeningIfSignal,
  stopListeningIfSignal,
} = require('internal/process/signal');
process.removeListener('newListener', startListeningIfSignal);
process.removeListener('removeListener', stopListeningIfSignal);

// ---- keep the attachment of the wrappers above so that it's easier to ----
// ----              compare the setups side-by-side                    -----

const {
  createWorkerStdio,
} = require('internal/worker/io');

let workerStdio;
function lazyWorkerStdio() {
  if (!workerStdio) workerStdio = createWorkerStdio();
  return workerStdio;
}

function getStdout() { return lazyWorkerStdio().stdout; }

function getStderr() { return lazyWorkerStdio().stderr; }

function getStdin() { return lazyWorkerStdio().stdin; }
