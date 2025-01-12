const { isInternalThread } = require('node:worker_threads');

console.log(`isInternalThread: ${isInternalThread}`);