const { isInternalThread, parentPort } = require('node:worker_threads');

parentPort.postMessage(`isInternalThread: ${isInternalThread}`);