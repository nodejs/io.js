const { Worker } = require('worker_threads');

new Worker(__dirname + '/worker.js', { type: 'module' });
